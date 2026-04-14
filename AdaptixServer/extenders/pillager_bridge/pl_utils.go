package main

import (
	"encoding/binary"
	"errors"
	"strings"

	adaptix "github.com/Adaptix-Framework/axc2"
)

const (
	COMMAND_EXEC_BOF = 50
)

func PackArray(array []interface{}) ([]byte, error) {
	var packData []byte
	for i := range array {
		switch v := array[i].(type) {
		case []byte:
			packData = append(packData, v...)
		case string:
			val := v
			if len(val) != 0 && !strings.HasSuffix(val, "\x00") {
				val += "\x00"
			}
			sz := make([]byte, 4)
			binary.LittleEndian.PutUint32(sz, uint32(len(val)))
			packData = append(packData, sz...)
			packData = append(packData, []byte(val)...)
		case int:
			num := make([]byte, 4)
			binary.LittleEndian.PutUint32(num, uint32(v))
			packData = append(packData, num...)
		case bool:
			bt := []byte{0}
			if v {
				bt[0] = 1
			}
			packData = append(packData, bt...)
		default:
			return nil, errors.New("PackArray unknown type")
		}
	}
	return packData, nil
}

type Teamserver interface {
	TsTaskCreate(agentId string, cmdline string, client string, taskData adaptix.TaskData)
	TsAgentConsoleOutput(agentId string, messageType int, message string, clearText string, store bool)
	TsServiceSendDataClient(operator string, service string, data string)
}
