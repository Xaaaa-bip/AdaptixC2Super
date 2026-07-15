package server

import (
	"AdaptixServer/core/axscript"
	"AdaptixServer/core/utils/logs"
	"AdaptixServer/core/utils/std"
	"encoding/json"
	"fmt"
	"net"
	"strings"

	adaptix "github.com/Adaptix-Framework/axc2"
)

func (ts *Teamserver) TsAxScriptLoadAgent(agentName string, axScript string, listeners []string) error {
	if ts.ScriptManager == nil {
		return fmt.Errorf("script manager not initialized")
	}
	return ts.ScriptManager.LoadAgentScript(agentName, axScript, listeners)
}

////////////////////

func (ts *Teamserver) AxGetAgentContext(agentId string) (agentName string, listenerRegName string, osType int, err error) {
	agent, err := ts.getAgent(agentId)
	if err != nil {
		return "", "", 0, err
	}
	data := agent.GetData()
	regName, _ := ts.TsListenerRegByName(data.Listener)
	return data.Name, regName, data.Os, nil
}

func (ts *Teamserver) AxGetAgents() map[string]interface{} {
	result := make(map[string]interface{})

	ts.Agents.ForEach(func(key string, value interface{}) bool {
		agent, ok := value.(*Agent)
		if !ok {
			return true
		}
		data := agent.GetData()
		agentMap := map[string]interface{}{
			"id":           data.Id,
			"type":         data.Name,
			"listener":     data.Listener,
			"external_ip":  data.ExternalIP,
			"internal_ip":  data.InternalIP,
			"domain":       data.Domain,
			"computer":     data.Computer,
			"username":     data.Username,
			"impersonated": data.Impersonated,
			"process":      data.Process,
			"arch":         data.Arch,
			"pid":          data.Pid,
			"tid":          data.Tid,
			"gmt":          data.GmtOffset,
			"acp":          data.ACP,
			"oemcp":        data.OemCP,
			"elevated":     data.Elevated,
			"tags":         data.Tags,
			"async":        data.Async,
			"sleep":        data.Sleep,
			"os_full":      data.OsDesc,
			"os":           osToString(data.Os),
		}
		result[data.Id] = agentMap
		return true
	})

	return result
}

func (ts *Teamserver) AxGetAgentInfo(agentId string, property string) interface{} {
	agent, err := ts.getAgent(agentId)
	if err != nil {
		return nil
	}
	data := agent.GetData()

	switch property {
	case "id":
		return data.Id
	case "type":
		return data.Name
	case "listener":
		return data.Listener
	case "external_ip":
		return data.ExternalIP
	case "internal_ip":
		return data.InternalIP
	case "domain":
		return data.Domain
	case "computer":
		return data.Computer
	case "username":
		return data.Username
	case "impersonated":
		return data.Impersonated
	case "process":
		return data.Process
	case "arch":
		return data.Arch
	case "pid":
		return data.Pid
	case "tid":
		return data.Tid
	case "gmt":
		return data.GmtOffset
	case "acp":
		return data.ACP
	case "oemcp":
		return data.OemCP
	case "elevated":
		return data.Elevated
	case "tags":
		return data.Tags
	case "async":
		return data.Async
	case "sleep":
		return data.Sleep
	case "os_full":
		return data.OsDesc
	case "os":
		return osToString(data.Os)
	default:
		return nil
	}
}

// /---
func (ts *Teamserver) AxGetAgentIds() []string {
	var ids []string
	ts.Agents.ForEach(func(key string, value interface{}) bool {
		ids = append(ids, key)
		return true
	})
	return ids
}

// /---
func (ts *Teamserver) AxGetCredentials() []interface{} {
	jsonStr, err := ts.TsCredentilsList()
	if err != nil {
		return []interface{}{}
	}
	var result []interface{}
	_ = json.Unmarshal([]byte(jsonStr), &result)
	if result == nil {
		return []interface{}{}
	}
	return result
}

// /---
func (ts *Teamserver) AxGetTargets() []interface{} {
	jsonStr, err := ts.TsTargetsList()
	if err != nil {
		return []interface{}{}
	}
	var result []interface{}
	_ = json.Unmarshal([]byte(jsonStr), &result)
	if result == nil {
		return []interface{}{}
	}
	return result
}

// /---
func (ts *Teamserver) TsAxScriptLoadUser(name string, script string) error {
	if ts.ScriptManager == nil {
		return fmt.Errorf("script manager not initialized")
	}
	return ts.ScriptManager.LoadUserScript(name, script)
}

// /---
func (ts *Teamserver) TsAxScriptUnloadUser(name string) error {
	if ts.ScriptManager == nil {
		return fmt.Errorf("script manager not initialized")
	}
	return ts.ScriptManager.UnloadUserScript(name)
}

// /---
func (ts *Teamserver) TsAxScriptList() (string, error) {
	if ts.ScriptManager == nil {
		return "[]", nil
	}
	scripts := ts.ScriptManager.ListScripts()
	data, err := json.Marshal(scripts)
	if err != nil {
		return "", err
	}
	return string(data), nil
}

// /---
func (ts *Teamserver) TsAxScriptCommands() (string, error) {
	if ts.ScriptManager == nil {
		return "{}", nil
	}
	return ts.ScriptManager.GetCommandsJSON()
}

// /---
func (ts *Teamserver) TsAxScriptParseAndExecute(agentId string, username string, cmdline string) error {
	if ts.ScriptManager == nil {
		return fmt.Errorf("script manager not initialized")
	}

	agentName, listenerRegName, agentOs, err := ts.AxGetAgentContext(agentId)
	if err != nil {
		return fmt.Errorf("agent not found: %w", err)
	}

	// Qt 客户端本地 Commander 处理 help；Web/raw 入口对齐为服务端本地回显
	if tokens := axscript.Tokenize(cmdline); len(tokens) > 0 && strings.EqualFold(tokens[0], "help") {
		helpText := ts.formatAgentCommandHelp(agentName, listenerRegName, agentOs, tokens[1:])
		ts.TsAgentConsoleLocalCommand(agentId, username, cmdline, "", helpText)
		return nil
	}

	resolved, resolveErr := ts.ScriptManager.CommandStore.ResolveFromCmdline(agentName, listenerRegName, agentOs, cmdline)
	if resolveErr != nil {
		return fmt.Errorf("unknown command: %w", resolveErr)
	}

	parsed, parseErr := ts.ScriptManager.ParseCommandPublic(cmdline, resolved)
	if parseErr != nil {
		return fmt.Errorf("parse error: %w", parseErr)
	}

	if resolved.Engine != nil {
		if fileErr := ts.ScriptManager.ResolveFileArgsPublic(resolved.Engine, parsed); fileErr != nil {
			return fmt.Errorf("file arg error: %w", fileErr)
		}
	}

	cmdDef := resolved.Command
	if resolved.Subcommand != nil {
		cmdDef = resolved.Subcommand
	}

	if cmdDef.HasPreHook && cmdDef.PreHookFunc != nil && resolved.Engine != nil {
		preHookErr := ts.ScriptManager.ExecutePreHookPublic(resolved.Engine, cmdDef.PreHookFunc, agentId, cmdline, parsed.Args)
		if preHookErr != nil {
			ts.TsAgentConsoleOutputClient(agentId, username, CONSOLE_OUT_LOCAL_ERROR, cmdline, std.ExtractJsErrorMessage(preHookErr))
			return nil
		}
		//ts.TsAgentConsoleOutputClient(agentId, username, 0, fmt.Sprintf("[AxScript] %s", cmdline), "")
		return nil
	}

	hookId := ""
	handlerId := ""

	if cmdDef.HasPostHook && cmdDef.PostHookFunc != nil && resolved.Engine != nil {
		hookId = ts.ScriptManager.HookStore.RegisterPostHook(resolved.Engine, cmdDef.PostHookFunc, agentId, "server")
	}
	if cmdDef.HasHandler && cmdDef.HandlerFunc != nil && resolved.Engine != nil {
		handlerId = ts.ScriptManager.HookStore.RegisterHandler(resolved.Engine, cmdDef.HandlerFunc, agentId, "server")
	}

	return ts.TsAgentCommand(agentName, agentId, username, hookId, handlerId, cmdline, false, parsed.Args)
}

// formatAgentCommandHelp 对齐 Qt Commander::ProcessHelp 输出
func (ts *Teamserver) formatAgentCommandHelp(agentName, listener string, os int, parts []string) string {
	if ts.ScriptManager == nil {
		return "script manager not initialized"
	}

	groups := ts.ScriptManager.CommandStore.GetCommandsForAgent(agentName, listener, os)
	if len(groups) == 0 {
		return "No commands registered for this agent"
	}

	var b strings.Builder
	const totalWidth = 24

	if len(parts) == 0 {
		for _, g := range groups {
			b.WriteString("\n")
			src := g.Source
			if src == "" {
				src = g.ScriptName
			}
			if src != "" {
				fmt.Fprintf(&b, "  Group - %s (%s)\n", g.GroupName, src)
			} else {
				fmt.Fprintf(&b, "  Group - %s\n", g.GroupName)
			}
			b.WriteString("  =====================================\n")
			for _, cmd := range g.Commands {
				if len(cmd.Subcommands) == 0 {
					pad := totalWidth - len(cmd.Name)
					if pad < 1 {
						pad = 1
					}
					fmt.Fprintf(&b, "  %s%s      %s\n", cmd.Name, strings.Repeat(" ", pad), cmd.Description)
					continue
				}
				for _, sub := range cmd.Subcommands {
					name := cmd.Name + " " + sub.Name
					pad := totalWidth - len(name)
					if pad < 1 {
						pad = 1
					}
					fmt.Fprintf(&b, "  %s%s      %s\n", name, strings.Repeat(" ", pad), sub.Description)
				}
			}
		}
		b.WriteString("\n  help [command [subcommand]]\n")
		return b.String()
	}

	// help <cmd> [sub]
	cmdName := parts[0]
	var found *axscript.CommandDef
	for i := range groups {
		for j := range groups[i].Commands {
			if groups[i].Commands[j].Name == cmdName {
				found = &groups[i].Commands[j]
				break
			}
		}
		if found != nil {
			break
		}
	}
	if found == nil {
		return "Unknown command: " + cmdName
	}

	if len(parts) == 1 {
		b.WriteString("\n")
		fmt.Fprintf(&b, "  Command               : %s\n", found.Name)
		if found.Description != "" {
			fmt.Fprintf(&b, "  Description           : %s\n", found.Description)
		}
		if found.Example != "" {
			fmt.Fprintf(&b, "  Example               : %s\n", found.Example)
		}
		if len(found.Subcommands) > 0 {
			b.WriteString("\n")
			b.WriteString("  SubCommand                Description\n")
			b.WriteString("  ----------                -----------\n")
			for _, sub := range found.Subcommands {
				pad := 20 - len(sub.Name)
				if pad < 1 {
					pad = 1
				}
				fmt.Fprintf(&b, "  %s%s      %s\n", sub.Name, strings.Repeat(" ", pad), sub.Description)
			}
		} else if len(found.Args) > 0 {
			writeCommandArgsHelp(&b, found.Name, found.Args)
		}
		return b.String()
	}

	subName := parts[1]
	var foundSub *axscript.CommandDef
	for i := range found.Subcommands {
		if found.Subcommands[i].Name == subName {
			foundSub = &found.Subcommands[i]
			break
		}
	}
	if foundSub == nil {
		return "Unknown subcommand: " + subName
	}

	full := found.Name + " " + foundSub.Name
	fmt.Fprintf(&b, "  Command               : %s\n", full)
	if foundSub.Description != "" {
		fmt.Fprintf(&b, "  Description           : %s\n", foundSub.Description)
	}
	if foundSub.Example != "" {
		fmt.Fprintf(&b, "  Example               : %s\n", foundSub.Example)
	}
	if len(foundSub.Args) > 0 {
		writeCommandArgsHelp(&b, full, foundSub.Args)
	}
	return b.String()
}

func writeCommandArgsHelp(b *strings.Builder, fullName string, args []axscript.ArgumentDef) {
	var usage strings.Builder
	usage.WriteString(fullName)
	maxArg := 0
	type row struct {
		full string
		arg  axscript.ArgumentDef
	}
	rows := make([]row, 0, len(args))
	for _, arg := range args {
		fullarg := formatArgToken(arg)
		if len(fullarg) > maxArg {
			maxArg = len(fullarg)
		}
		usage.WriteString(" ")
		usage.WriteString(fullarg)
		rows = append(rows, row{full: fullarg, arg: arg})
	}
	b.WriteString("  Usage                 : ")
	b.WriteString(usage.String())
	b.WriteString("\n\n")
	b.WriteString("  Arguments:\n")
	for _, r := range rows {
		pad := maxArg - len(r.full)
		if pad < 0 {
			pad = 0
		}
		def := ""
		if r.arg.DefaultUsed {
			def = fmt.Sprintf(" (default: '%v').", r.arg.DefaultValue)
		}
		fmt.Fprintf(b, "    %s%s  : %-9s%s %s\n", r.full, strings.Repeat(" ", pad), r.arg.Type+".", def, r.arg.Description)
	}
}

func formatArgToken(arg axscript.ArgumentDef) string {
	req := arg.Required && !arg.DefaultUsed
	open, close := "[", "]"
	if req {
		open, close = "<", ">"
	}
	mid := arg.Mark
	if arg.Mark != "" && arg.Name != "" {
		mid = arg.Mark + " " + arg.Name
	} else if arg.Name != "" {
		mid = arg.Name
	}
	return open + mid + close
}

func (ts *Teamserver) TsAxScriptResolveHooks(agentName string, agentId string, listenerRegName string, os int, cmdline string, args map[string]interface{}) (string, string, bool, error) {
	if ts.ScriptManager == nil {
		return "", "", false, nil
	}
	return ts.ScriptManager.ResolveAndExecutePreHook(agentName, agentId, listenerRegName, os, cmdline, args)
}

func (ts *Teamserver) TsAxScriptExecPostHook(hookId string, data map[string]interface{}) (map[string]interface{}, error) {
	if ts.ScriptManager == nil {
		return data, nil
	}
	return ts.ScriptManager.HookStore.ExecutePostHook(hookId, data)
}

// /---
func (ts *Teamserver) TsAxScriptExecHandler(handlerId string, data map[string]interface{}) error {
	if ts.ScriptManager == nil {
		return nil
	}
	return ts.ScriptManager.HookStore.ExecuteHandler(handlerId, data)
}

func (ts *Teamserver) TsAxScriptRemovePostHook(hookId string) {
	if ts.ScriptManager == nil {
		return
	}
	ts.ScriptManager.HookStore.RemovePostHook(hookId)
}

// /---
func (ts *Teamserver) TsAxScriptRemoveHandler(handlerId string) {
	if ts.ScriptManager == nil {
		return
	}
	ts.ScriptManager.HookStore.RemoveHandler(handlerId)
}

func (ts *Teamserver) TsAxScriptIsServerHook(id string) bool {
	if ts.ScriptManager == nil {
		return false
	}
	return ts.ScriptManager.HookStore.IsServerHook(id)
}

func (ts *Teamserver) TsPresyncAxScriptData() []interface{} {
	if ts.ScriptManager == nil {
		return nil
	}

	scripts := ts.ScriptManager.ListProfileScriptsWithContent()
	batches := ts.ScriptManager.CommandStore.GetProfileAndUserCommands()

	if len(scripts) == 0 && len(batches) == 0 {
		return nil
	}

	type scriptData struct {
		content string
		groups  []AxCommandBatch
	}
	scriptsMap := make(map[string]*scriptData)

	for _, s := range scripts {
		scriptsMap[s.Name] = &scriptData{
			content: s.Script,
			groups:  []AxCommandBatch{},
		}
	}

	for _, batch := range batches {
		if len(batch.Groups) == 0 {
			continue
		}

		for _, group := range batch.Groups {
			scriptName := group.ScriptName
			if scriptName == "" {
				scriptName = "_unknown_"
			}

			data, err := json.Marshal([]interface{}{group})
			if err != nil {
				logs.Error("", "Presync marshal error for group '%s': %v", group.GroupName, err)
				continue
			}

			entry, exists := scriptsMap[scriptName]
			if !exists {
				entry = &scriptData{
					content: "",
					groups:  []AxCommandBatch{},
				}
				scriptsMap[scriptName] = entry
			}

			entry.groups = append(entry.groups, AxCommandBatch{
				Agent:    batch.Agent,
				Listener: batch.Listener,
				Os:       batch.Os,
				Commands: string(data),
			})
		}
	}

	var packets []interface{}
	for name, data := range scriptsMap {
		packets = append(packets, CreateSpAxScriptData(name, data.content, data.groups))
	}
	return packets
}

// /---
func (ts *Teamserver) TsAxScriptBroadcastData() {
	packets := ts.TsPresyncAxScriptData()
	for _, p := range packets {
		ts.TsSyncAllClients(p)
	}
}

func (ts *Teamserver) TsGetAgentCommandGroups(agentName string) []AxCommandBatch {
	if ts.ScriptManager == nil {
		return nil
	}

	batches := ts.ScriptManager.CommandStore.GetAgentCommandBatches(agentName)
	var result []AxCommandBatch

	for _, batch := range batches {
		if len(batch.Groups) == 0 {
			continue
		}

		data, err := json.Marshal(batch.Groups)
		if err != nil {
			logs.Error("", "Marshal error for agent '%s': %v", agentName, err)
			continue
		}

		result = append(result, AxCommandBatch{
			Agent:    batch.Agent,
			Listener: batch.Listener,
			Os:       batch.Os,
			Commands: string(data),
		})
	}

	return result
}

// /---
func (ts *Teamserver) AxCredentialsAdd(creds []map[string]interface{}) error {
	return ts.TsCredentilsAdd(creds)
}

// /---
func (ts *Teamserver) AxTargetsAdd(targets []map[string]interface{}) error {
	return ts.TsTargetsAdd(targets)
}

// /---
func (ts *Teamserver) AxAgentRemove(agentIds []string) error {
	for _, id := range agentIds {
		_ = ts.TsAgentRemove(id)
	}
	return nil
}

// /---
func (ts *Teamserver) AxAgentSetTag(agentIds []string, tag string) error {
	for _, id := range agentIds {
		updateData := map[string]interface{}{"tags": tag}
		_ = ts.TsAgentUpdateDataPartial(id, updateData)
	}
	return nil
}

// /---
func (ts *Teamserver) AxAgentSetMark(agentIds []string, mark string) error {
	for _, id := range agentIds {
		updateData := map[string]interface{}{"mark": mark}
		_ = ts.TsAgentUpdateDataPartial(id, updateData)
	}
	return nil
}

// /---
func (ts *Teamserver) AxAgentSetColor(agentIds []string, background string, foreground string, reset bool) error {
	// Agent color is a client-only visual property, no server-side storage
	return nil
}

func (ts *Teamserver) AxAgentUpdateData(agentId string, updateData map[string]interface{}) error {
	return ts.TsAgentUpdateDataPartial(agentId, updateData)
}

func (ts *Teamserver) TsAxScriptLoadFromProfile() {
	if ts.ScriptManager == nil {
		return
	}

	if ts.Profile == nil || ts.Profile.Server == nil {
		return
	}

	for _, scriptPath := range ts.Profile.Server.AxScripts {
		err := ts.ScriptManager.LoadAxScript(scriptPath)
		if err != nil {
			logs.Error("", "Failed to load profile axscript '%s': %v", scriptPath, err)
		}
	}
}

// /---
func (ts *Teamserver) AxGetDownloads() []interface{} {
	jsonStr, err := ts.TsDownloadList()
	if err != nil {
		return []interface{}{}
	}
	var result []interface{}
	_ = json.Unmarshal([]byte(jsonStr), &result)
	if result == nil {
		return []interface{}{}
	}
	return result
}

// /---
func (ts *Teamserver) AxGetScreenshots() []interface{} {
	jsonStr, err := ts.TsScreenshotList()
	if err != nil {
		return []interface{}{}
	}
	var result []interface{}
	_ = json.Unmarshal([]byte(jsonStr), &result)
	if result == nil {
		return []interface{}{}
	}
	return result
}

// /---
func (ts *Teamserver) AxGetTunnels() []interface{} {
	jsonStr, err := ts.TsTunnelList()
	if err != nil {
		return []interface{}{}
	}
	var result []interface{}
	_ = json.Unmarshal([]byte(jsonStr), &result)
	if result == nil {
		return []interface{}{}
	}
	return result
}

// /---
func (ts *Teamserver) AxGetInterfaces() []string {
	var result []string
	ifaces, err := net.Interfaces()
	if err != nil {
		return result
	}
	for _, iface := range ifaces {
		addrs, err := iface.Addrs()
		if err != nil {
			continue
		}
		for _, addr := range addrs {
			var ip net.IP
			switch v := addr.(type) {
			case *net.IPNet:
				ip = v.IP
			case *net.IPAddr:
				ip = v.IP
			}
			if ip != nil && !ip.IsLoopback() {
				result = append(result, ip.String())
			}
		}
	}
	return result
}

// /---
func (ts *Teamserver) AxGetAgentMark(agentId string) string {
	agent, err := ts.getAgent(agentId)
	if err != nil {
		return ""
	}
	data := agent.GetData()
	return data.Mark
}

// /---
func (ts *Teamserver) AxUnloadAxScript(name string) error {
	if ts.ScriptManager == nil {
		return fmt.Errorf("script manager not initialized")
	}
	return ts.ScriptManager.UnloadUserScript(name)
}

func osToString(os int) string {
	switch os {
	case adaptix.OS_WINDOWS:
		return "windows"
	case adaptix.OS_LINUX:
		return "linux"
	case adaptix.OS_MAC:
		return "macos"
	default:
		return "unknown"
	}
}
