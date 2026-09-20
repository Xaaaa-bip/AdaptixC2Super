package functions

import (
	"archive/zip"
	"bytes"
	"encoding/binary"
	"errors"
	"fmt"
	"image/png"
	"io"
	"io/fs"
	"net"
	"os"
	"path/filepath"
	"runtime"

	"github.com/kbinani/screenshot"
)

/// FS

func CopyFile(src, dst string, info fs.FileInfo) error {
	source, err := os.Open(src)
	if err != nil {
		return err
	}
	defer func(source *os.File) {
		_ = source.Close()
	}(source)

	var mode os.FileMode = 0644
	if runtime.GOOS != "windows" {
		mode = info.Mode()
	}

	dest, err := os.OpenFile(dst, os.O_RDWR|os.O_CREATE|os.O_TRUNC, mode)
	if err != nil {
		return err
	}
	defer func(dest *os.File) {
		_ = dest.Close()
	}(dest)

	_, err = io.Copy(dest, source)
	return err
}

func CopyDir(srcDir, dstDir string) error {
	srcInfo, err := os.Stat(srcDir)
	if err != nil {
		return err
	}

	var mode os.FileMode = 0755
	if runtime.GOOS != "windows" {
		mode = srcInfo.Mode()
	}

	err = os.MkdirAll(dstDir, mode)
	if err != nil {
		return err
	}

	entries, err := os.ReadDir(srcDir)
	if err != nil {
		return err
	}

	for _, entry := range entries {
		srcPath := filepath.Join(srcDir, entry.Name())
		dstPath := filepath.Join(dstDir, entry.Name())

		info, err := entry.Info()
		if err != nil {
			return err
		}

		if info.IsDir() {
			err = CopyDir(srcPath, dstPath)
			if err != nil {
				return err
			}
		} else {
			err = CopyFile(srcPath, dstPath, info)
			if err != nil {
				return err
			}
		}
	}
	return nil
}

/// ZIP

func ZipBytes(data []byte, name string) ([]byte, error) {
	var buf bytes.Buffer
	zipWriter := zip.NewWriter(&buf)

	writer, err := zipWriter.Create(name)
	if err != nil {
		return nil, err
	}

	_, err = writer.Write(data)
	if err != nil {
		return nil, err
	}

	err = zipWriter.Close()
	if err != nil {
		return nil, err
	}

	return buf.Bytes(), nil
}

func UnzipBytes(zipData []byte) (map[string][]byte, error) {
	result := make(map[string][]byte)
	reader := bytes.NewReader(zipData)

	zipReader, err := zip.NewReader(reader, int64(len(zipData)))
	if err != nil {
		return nil, err
	}

	for _, file := range zipReader.File {
		rc, err := file.Open()
		if err != nil {
			return nil, err
		}
		defer rc.Close()

		var buf bytes.Buffer
		_, err = io.Copy(&buf, rc)
		if err != nil {
			return nil, err
		}

		result[file.Name] = buf.Bytes()
	}

	return result, nil
}

func ZipFile(srcFilePath string) ([]byte, error) {
	buf := new(bytes.Buffer)
	zipWriter := zip.NewWriter(buf)

	fileToZip, err := os.Open(srcFilePath)
	if err != nil {
		return nil, err
	}
	defer fileToZip.Close()

	info, err := fileToZip.Stat()
	if err != nil {
		return nil, err
	}

	header, err := zip.FileInfoHeader(info)
	if err != nil {
		return nil, err
	}
	header.Name = filepath.Base(srcFilePath)
	header.Method = zip.Deflate

	writer, err := zipWriter.CreateHeader(header)
	if err != nil {
		return nil, err
	}

	_, err = io.Copy(writer, fileToZip)
	if err != nil {
		return nil, err
	}

	if err := zipWriter.Close(); err != nil {
		return nil, err
	}

	return buf.Bytes(), nil
}

func UnzipFile(zipPath string, targetDir string) error {
	r, err := zip.OpenReader(zipPath)
	if err != nil {
		return err
	}
	defer r.Close()

	for _, f := range r.File {
		destPath := filepath.Join(targetDir, f.Name)

		// Создание директорий
		if f.FileInfo().IsDir() {
			err = os.MkdirAll(destPath, os.ModePerm)
			if err != nil {
				return err
			}
			continue
		}

		err = os.MkdirAll(filepath.Dir(destPath), os.ModePerm)
		if err != nil {
			return err
		}

		dstFile, err := os.Create(destPath)
		if err != nil {
			return err
		}
		defer dstFile.Close()

		srcFile, err := f.Open()
		if err != nil {
			return err
		}
		defer srcFile.Close()

		_, err = io.Copy(dstFile, srcFile)
		if err != nil {
			return err
		}
	}

	return nil
}

func ZipDirectory(srcDir string) ([]byte, error) {
	buf := new(bytes.Buffer)
	zipWriter := zip.NewWriter(buf)

	err := filepath.Walk(srcDir, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}

		relPath, err := filepath.Rel(srcDir, path)
		if err != nil {
			return err
		}
		if info.IsDir() {
			if relPath == "." {
				return nil
			}
			relPath += "/"
			_, err = zipWriter.Create(relPath)
			return err
		}

		file, err := os.Open(path)
		if err != nil {
			return err
		}
		defer file.Close()

		header, err := zip.FileInfoHeader(info)
		if err != nil {
			return err
		}
		header.Name = relPath
		header.Method = zip.Deflate

		writer, err := zipWriter.CreateHeader(header)
		if err != nil {
			return err
		}

		_, err = io.Copy(writer, file)
		return err
	})
	if err != nil {
		return nil, err
	}

	if err := zipWriter.Close(); err != nil {
		return nil, err
	}

	return buf.Bytes(), nil
}

func UnzipDirectory(zipData []byte, targetDir string) error {
	reader := bytes.NewReader(zipData)
	zipReader, err := zip.NewReader(reader, int64(len(zipData)))
	if err != nil {
		return err
	}

	for _, f := range zipReader.File {
		destPath := filepath.Join(targetDir, f.Name)

		if f.FileInfo().IsDir() {
			err := os.MkdirAll(destPath, os.ModePerm)
			if err != nil {
				return err
			}
			continue
		}

		err := os.MkdirAll(filepath.Dir(destPath), os.ModePerm)
		if err != nil {
			return err
		}

		dstFile, err := os.Create(destPath)
		if err != nil {
			return err
		}
		defer dstFile.Close()

		srcFile, err := f.Open()
		if err != nil {
			return err
		}
		defer srcFile.Close()

		_, err = io.Copy(dstFile, srcFile)
		if err != nil {
			return err
		}
	}

	return nil
}

/// SCREENS

func Screenshots() (map[int][]byte, error) {
	result := make(map[int][]byte)
	num := screenshot.NumActiveDisplays()
	for i := 0; i < num; i++ {
		img, err := screenshot.CaptureRect(screenshot.GetDisplayBounds(i))
		if err != nil {
			return nil, err
		}
		buf := new(bytes.Buffer)
		err = png.Encode(buf, img)
		if err != nil {
			return nil, err
		}
		result[i] = buf.Bytes()
	}
	return result, nil
}

/// NET

func ConnRead(conn net.Conn, size int) ([]byte, error) {
	if size <= 0 {
		return nil, fmt.Errorf("incorrected size: %d", size)
	}

	message := make([]byte, 0, size)
	tmpBuff := make([]byte, 1024)
	readSize := 0

	for readSize < size {
		toRead := size - readSize
		if toRead < len(tmpBuff) {
			tmpBuff = tmpBuff[:toRead]
		}

		n, err := conn.Read(tmpBuff)
		if err != nil {
			return nil, err
		}

		message = append(message, tmpBuff[:n]...)
		readSize += n
	}
	return message, nil
}

// maxRecvMsgSize 是收包长度的上界。
//
// 长度前缀是 4 字节大端，对端（或一条错位的流）可以把它写成 4 GB，而 ConnRead 第一件事
// 就是 make([]byte, 0, size) ⇒ 长度直接变成一次 4 GB 的分配，之后还会按 1 KB 的块一直
// 读到那个长度为止（那是永远读不满的：实际表现是要么内存被撑爆让系统杀掉，要么永久卡在
// 循环里不再回连）。合法流量远小于此：服务端给这类 agent 的上传分片是 0x500000（5 MB，
// gopher / macos / linux 三处一致），其余消息都在百 KB 量级。取 64 MB 是 12 倍余量，
// 只用来把明显不可能的包长挡在分配之前 —— 正常的包一字节都不受影响。
//
// ★ 本函数与 macos_agent/src_macos/functions/functions.go 的同名函数**逐字节相同**。
// 苹果那份 2026-09-19 先修（§6.19），本处是同一缺陷的第二份，随 ② 收尾一并修（§6.20）。
// 两份若再分叉，请一起改。
const maxRecvMsgSize = 64 << 20

func RecvMsg(conn net.Conn) ([]byte, error) {
	bufLen, err := ConnRead(conn, 4)
	if err != nil {
		return nil, err
	}
	msgLen := binary.BigEndian.Uint32(bufLen)
	if msgLen > maxRecvMsgSize {
		return nil, fmt.Errorf("message too large: %d bytes (max %d)", msgLen, maxRecvMsgSize)
	}

	return ConnRead(conn, int(msgLen))
}

func SendMsg(conn net.Conn, data []byte) error {
	if conn == nil {
		return errors.New("conn is nil")
	}

	msgLen := make([]byte, 4)
	binary.BigEndian.PutUint32(msgLen, uint32(len(data)))
	message := append(msgLen, data...)
	_, err := conn.Write(message)
	return err
}
