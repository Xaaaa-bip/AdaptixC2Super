var metadata = {
 name: "Web Delivery",
 description: "HTTP-based agent delivery with automatic platform detection via User-Agent"
};

var Elements = {
 // Network
 comboHostBind: null,
 spinPortBind: null,
 checkSSL: null,

 // GopherTCP config
 textCallbackAddr: null,
 textEncryptKey: null,
 textReconnTimeout: null,
 spinReconnCount: null,

 // Token
 textToken: null,

 // Platforms
 checkWinAmd64: null,
 checkWinArm64: null,
 checkLinuxAmd64: null,
 checkLinuxArm64: null,
 checkDarwinAmd64: null,
 checkDarwinArm64: null,

 // Buttons
 btnStart: null,
 btnStop: null,
 btnGenerateKey: null,
 btnGenerateToken: null,

 // Status
 labelStatus: null,
 textDeliveryURL: null,

 // One-liners
 textPSOneliner: null,
 textBashOneliner: null,
 textPyOneliner: null,

 // Logs
 tableLogs: null
};

function getSelectedPlatforms() {
 var platforms = [];
 if (Elements.checkWinAmd64.isChecked()) platforms.push("windows/amd64");
 if (Elements.checkWinArm64.isChecked()) platforms.push("windows/arm64");
 if (Elements.checkLinuxAmd64.isChecked()) platforms.push("linux/amd64");
 if (Elements.checkLinuxArm64.isChecked()) platforms.push("linux/arm64");
 if (Elements.checkDarwinAmd64.isChecked()) platforms.push("darwin/amd64");
 if (Elements.checkDarwinArm64.isChecked()) platforms.push("darwin/arm64");
 return platforms;
}

function buildConfig() {
 return {
  host_bind: Elements.comboHostBind.currentText(),
  port_bind: Elements.spinPortBind.value(),
  delivery_token: Elements.textToken.text(),
  callback_addresses: Elements.textCallbackAddr.text(),
  encrypt_key: Elements.textEncryptKey.text(),
  ssl: Elements.checkSSL.isChecked(),
  reconnect_timeout: Elements.textReconnTimeout.text(),
  reconnect_count: Elements.spinReconnCount.value(),
  platforms: getSelectedPlatforms(),
  default_platform: "windows/amd64",
  server_headers: ""
 };
}

function copyToClipboard(text) {
 // Use ax clipboard if available
 if (typeof ax !== 'undefined' && ax.clipboard_set) {
  ax.clipboard_set(text);
 } else {
  // Fallback: select the text field content
 }
}

function buildConfigPage() {
 // Network section
 Elements.comboHostBind = form.create_combo();
 Elements.comboHostBind.addItems(["0.0.0.0", "127.0.0.1"]);
 Elements.comboHostBind.setCurrentIndex(0);

 Elements.spinPortBind = form.create_spin(1, 65535);
 Elements.spinPortBind.setValue(8888);

 Elements.checkSSL = form.create_check("Enable SSL/TLS");

 var btnGenKey = form.create_button("Generate");
 form.connect(btnGenKey, "clicked", function() {
  ax.service_command("web_delivery", "generate_key", {});
 });

 Elements.textEncryptKey = form.create_textline("");
 Elements.textEncryptKey.setPlaceholder("32 hex characters (e.g. a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6)");

 var btnGenToken = form.create_button("Generate");
 form.connect(btnGenToken, "clicked", function() {
  ax.service_command("web_delivery", "generate_token", {});
 });

 Elements.textToken = form.create_textline("");
 Elements.textToken.setPlaceholder("Auto-generated if empty");

 Elements.textCallbackAddr = form.create_textmulti("");
 Elements.textCallbackAddr.setPlaceholder("GopherTCP listener address (host:port)\ne.g. 10.0.0.1:4444");

 Elements.textReconnTimeout = form.create_textline("30s");
 Elements.textReconnTimeout.setPlaceholder("e.g. 30s, 5m, 1h");

 Elements.spinReconnCount = form.create_spin(1, 1000);
 Elements.spinReconnCount.setValue(10);

 // Network grid
 var netGrid = form.create_gridlayout();
 netGrid.addWidget(form.create_label("Host Bind:"), 0, 0, 1, 1);
 netGrid.addWidget(Elements.comboHostBind, 0, 1, 1, 1);
 netGrid.addWidget(form.create_label("Port:"), 1, 0, 1, 1);
 netGrid.addWidget(Elements.spinPortBind, 1, 1, 1, 1);
 netGrid.addWidget(Elements.checkSSL, 2, 0, 1, 2);
 netGrid.addWidget(form.create_hline(), 3, 0, 1, 2);
 netGrid.addWidget(form.create_label("Encrypt Key:"), 4, 0, 1, 1);
 netGrid.addWidget(Elements.textEncryptKey, 4, 1, 1, 1);
 netGrid.addWidget(btnGenKey, 5, 1, 1, 1);
 netGrid.addWidget(form.create_label("Delivery Token:"), 6, 0, 1, 1);
 netGrid.addWidget(Elements.textToken, 6, 1, 1, 1);
 netGrid.addWidget(btnGenToken, 7, 1, 1, 1);
 netGrid.addWidget(form.create_label("Callback Addresses:"), 8, 0, 1, 1);
 netGrid.addWidget(Elements.textCallbackAddr, 8, 1, 1, 1);
 netGrid.addWidget(form.create_label("Reconnect Timeout:"), 9, 0, 1, 1);
 netGrid.addWidget(Elements.textReconnTimeout, 9, 1, 1, 1);
 netGrid.addWidget(form.create_label("Reconnect Count:"), 10, 0, 1, 1);
 netGrid.addWidget(Elements.spinReconnCount, 10, 1, 1, 1);

 var netPanel = form.create_panel();
 netPanel.setLayout(netGrid);

 // Platform checkboxes
 Elements.checkWinAmd64 = form.create_check("Windows x64");
 Elements.checkWinAmd64.setChecked(true);
 Elements.checkWinArm64 = form.create_check("Windows ARM64");
 Elements.checkLinuxAmd64 = form.create_check("Linux x64");
 Elements.checkLinuxAmd64.setChecked(true);
 Elements.checkLinuxArm64 = form.create_check("Linux ARM64");
 Elements.checkDarwinAmd64 = form.create_check("macOS Intel");
 Elements.checkDarwinArm64 = form.create_check("macOS Apple Silicon");

 var btnSelectAll = form.create_button("Select All");
 form.connect(btnSelectAll, "clicked", function() {
  Elements.checkWinAmd64.setChecked(true);
  Elements.checkWinArm64.setChecked(true);
  Elements.checkLinuxAmd64.setChecked(true);
  Elements.checkLinuxArm64.setChecked(true);
  Elements.checkDarwinAmd64.setChecked(true);
  Elements.checkDarwinArm64.setChecked(true);
 });

 var btnDeselectAll = form.create_button("Deselect All");
 form.connect(btnDeselectAll, "clicked", function() {
  Elements.checkWinAmd64.setChecked(false);
  Elements.checkWinArm64.setChecked(false);
  Elements.checkLinuxAmd64.setChecked(false);
  Elements.checkLinuxArm64.setChecked(false);
  Elements.checkDarwinAmd64.setChecked(false);
  Elements.checkDarwinArm64.setChecked(false);
 });

 var platGrid = form.create_gridlayout();
 platGrid.addWidget(Elements.checkWinAmd64, 0, 0, 1, 1);
 platGrid.addWidget(Elements.checkWinArm64, 0, 1, 1, 1);
 platGrid.addWidget(Elements.checkLinuxAmd64, 1, 0, 1, 1);
 platGrid.addWidget(Elements.checkLinuxArm64, 1, 1, 1, 1);
 platGrid.addWidget(Elements.checkDarwinAmd64, 2, 0, 1, 1);
 platGrid.addWidget(Elements.checkDarwinArm64, 2, 1, 1, 1);
 platGrid.addWidget(btnSelectAll, 3, 0, 1, 1);
 platGrid.addWidget(btnDeselectAll, 3, 1, 1, 1);

 var platPanel = form.create_panel();
 platPanel.setLayout(platGrid);

 // Config tab: network + platforms side by side
 var configHLayout = form.create_hlayout();
 configHLayout.addWidget(netPanel);
 configHLayout.addWidget(platPanel);

 var configPanel = form.create_panel();
 configPanel.setLayout(configHLayout);

 return configPanel;
}

function buildDeliveryPage() {
 // Status
 Elements.labelStatus = form.create_label("Stopped");
 Elements.labelStatus.setStyleSheet("font-weight: bold; font-size: 14px;");

 // Start/Stop buttons
 Elements.btnStart = form.create_button("Start Delivery");
 Elements.btnStop = form.create_button("Stop Delivery");
 Elements.btnStop.setEnabled(false);

 form.connect(Elements.btnStart, "clicked", function() {
  var cfg = buildConfig();
  ax.service_command("web_delivery", "start", cfg);
 });

 form.connect(Elements.btnStop, "clicked", function() {
  ax.service_command("web_delivery", "stop", {});
 });

 // Delivery URL
 Elements.textDeliveryURL = form.create_textline("");
 Elements.textDeliveryURL.setReadOnly(true);
 Elements.textDeliveryURL.setPlaceholder("Delivery URL will appear here after starting...");

 var btnCopyURL = form.create_button("Copy");
 form.connect(btnCopyURL, "clicked", function() {
  copyToClipboard(Elements.textDeliveryURL.text());
 });

 // One-liners
 Elements.textPSOneliner = form.create_textmulti("");
 Elements.textPSOneliner.setReadOnly(true);
 Elements.textPSOneliner.setPlaceholder("PowerShell one-liner...");

 var btnCopyPS = form.create_button("Copy PS");
 form.connect(btnCopyPS, "clicked", function() {
  copyToClipboard(Elements.textPSOneliner.text());
 });

 Elements.textBashOneliner = form.create_textmulti("");
 Elements.textBashOneliner.setReadOnly(true);
 Elements.textBashOneliner.setPlaceholder("Bash one-liner...");

 var btnCopyBash = form.create_button("Copy Bash");
 form.connect(btnCopyBash, "clicked", function() {
  copyToClipboard(Elements.textBashOneliner.text());
 });

 Elements.textPyOneliner = form.create_textmulti("");
 Elements.textPyOneliner.setReadOnly(true);
 Elements.textPyOneliner.setPlaceholder("Python one-liner...");

 var btnCopyPy = form.create_button("Copy Py");
 form.connect(btnCopyPy, "clicked", function() {
  copyToClipboard(Elements.textPyOneliner.text());
 });

 // Layout
 var topGrid = form.create_gridlayout();
 topGrid.addWidget(Elements.labelStatus, 0, 0, 1, 1);
 topGrid.addWidget(Elements.btnStart, 0, 1, 1, 1);
 topGrid.addWidget(Elements.btnStop, 0, 2, 1, 1);
 topGrid.addWidget(form.create_hline(), 1, 0, 1, 3);
 topGrid.addWidget(form.create_label("Delivery URL:"), 2, 0, 1, 1);
 topGrid.addWidget(Elements.textDeliveryURL, 2, 1, 1, 1);
 topGrid.addWidget(btnCopyURL, 2, 2, 1, 1);

 // One-liners in tabs
 var onelinerTabs = form.create_tabs();

 var psLayout = form.create_vlayout();
 psLayout.addWidget(Elements.textPSOneliner);
 psLayout.addWidget(btnCopyPS);
 var psPanel = form.create_panel();
 psPanel.setLayout(psLayout);
 onelinerTabs.addTab(psPanel, "PowerShell");

 var bashLayout = form.create_vlayout();
 bashLayout.addWidget(Elements.textBashOneliner);
 bashLayout.addWidget(btnCopyBash);
 var bashPanel = form.create_panel();
 bashPanel.setLayout(bashLayout);
 onelinerTabs.addTab(bashPanel, "Bash");

 var pyLayout = form.create_vlayout();
 pyLayout.addWidget(Elements.textPyOneliner);
 pyLayout.addWidget(btnCopyPy);
 var pyPanel = form.create_panel();
 pyPanel.setLayout(pyLayout);
 onelinerTabs.addTab(pyPanel, "Python");

 var mainLayout = form.create_vlayout();
 mainLayout.addLayout(topGrid);
 mainLayout.addWidget(form.create_hline());
 mainLayout.addWidget(onelinerTabs);

 var panel = form.create_panel();
 panel.setLayout(mainLayout);
 return panel;
}

function buildLogsPage() {
 Elements.tableLogs = form.create_table();
 Elements.tableLogs.setColumnCount(6);
 Elements.tableLogs.setHorizontalHeaderLabels(["Time", "IP", "User-Agent", "Platform", "Status", "Size"]);
 Elements.tableLogs.setColumnWidth(0, 140);
 Elements.tableLogs.setColumnWidth(1, 120);
 Elements.tableLogs.setColumnWidth(2, 250);
 Elements.tableLogs.setColumnWidth(3, 120);
 Elements.tableLogs.setColumnWidth(4, 80);
 Elements.tableLogs.setColumnWidth(5, 80);

 var btnRefresh = form.create_button("Refresh Logs");
 form.connect(btnRefresh, "clicked", function() {
  ax.service_command("web_delivery", "get_logs", {});
 });

 var btnClear = form.create_button("Clear");
 form.connect(btnClear, "clicked", function() {
  Elements.tableLogs.setRowCount(0);
 });

 var btnLayout = form.create_hlayout();
 btnLayout.addWidget(btnRefresh);
 btnLayout.addWidget(btnClear);

 var layout = form.create_vlayout();
 layout.addLayout(btnLayout);
 layout.addWidget(Elements.tableLogs);

 var panel = form.create_panel();
 panel.setLayout(layout);
 return panel;
}

function addLogEntry(entry) {
 var row = Elements.tableLogs.rowCount();
 Elements.tableLogs.insertRow(row);
 Elements.tableLogs.setItem(row, 0, entry.timestamp || "");
 Elements.tableLogs.setItem(row, 1, entry.ip || "");
 Elements.tableLogs.setItem(row, 2, entry.user_agent || "");
 Elements.tableLogs.setItem(row, 3, entry.platform || "");
 Elements.tableLogs.setItem(row, 4, entry.status || "");
 Elements.tableLogs.setItem(row, 5, (entry.size || 0).toString());

 // Auto-scroll to bottom
 Elements.tableLogs.scrollToBottom();
}

////

function InitService() {
 var tabs = form.create_tabs();
 tabs.addTab(buildConfigPage(), "Config");
 tabs.addTab(buildDeliveryPage(), "Delivery");
 tabs.addTab(buildLogsPage(), "Logs");

 var mainLayout = form.create_vlayout();
 mainLayout.addWidget(tabs);

 var dialog = form.create_ext_dialog("Web Delivery");
 dialog.setSize(850, 600);
 dialog.setLayout(mainLayout);
 dialog.setButtonsText("Close", "Cancel");

 // Request initial status
 ax.service_command("web_delivery", "status", {});
 ax.service_command("web_delivery", "get_logs", {});
}

function data_handler(data) {
 var response = JSON.parse(data);
 switch (response.action) {
 case "status":
 case "status_update":
  if (response.status === "running") {
   Elements.labelStatus.setText("Running");
   Elements.labelStatus.setStyleSheet("font-weight: bold; font-size: 14px; color: green;");
   Elements.btnStart.setEnabled(false);
   Elements.btnStop.setEnabled(true);
   if (response.url) {
    Elements.textDeliveryURL.setText(response.url);
   }
   if (response.oneliners) {
    if (response.oneliners.powershell) Elements.textPSOneliner.setText(response.oneliners.powershell);
    if (response.oneliners.bash) Elements.textBashOneliner.setText(response.oneliners.bash);
    if (response.oneliners.python) Elements.textPyOneliner.setText(response.oneliners.python);
   }
  } else {
   Elements.labelStatus.setText("Stopped");
   Elements.labelStatus.setStyleSheet("font-weight: bold; font-size: 14px; color: red;");
   Elements.btnStart.setEnabled(true);
   Elements.btnStop.setEnabled(false);
  }
  // Fill config from status if available
  if (response.config) {
   fillConfig(response.config);
  }
  break;

 case "config":
  if (response.config) {
   fillConfig(response.config);
  }
  break;

 case "start_result":
  if (response.success) {
   ax.show_message("Success", "Web Delivery started successfully!\n\nURL: " + (response.url || ""));
   if (response.url) {
    Elements.textDeliveryURL.setText(response.url);
   }
   if (response.oneliners) {
    if (response.oneliners.powershell) Elements.textPSOneliner.setText(response.oneliners.powershell);
    if (response.oneliners.bash) Elements.textBashOneliner.setText(response.oneliners.bash);
    if (response.oneliners.python) Elements.textPyOneliner.setText(response.oneliners.python);
   }
  } else {
   ax.show_message("Error", "Failed to start: " + (response.message || "Unknown error"));
  }
  break;

 case "stop_result":
  if (response.success) {
   ax.show_message("Success", "Web Delivery stopped.");
  }
  break;

 case "build_progress":
  if (response.status === "building") {
   Elements.labelStatus.setText("Building...");
   Elements.labelStatus.setStyleSheet("font-weight: bold; font-size: 14px; color: orange;");
   Elements.btnStart.setEnabled(false);
   Elements.btnStop.setEnabled(false);
  } else if (response.platform && response.status) {
   // Individual platform build progress
  }
  break;

 case "key_generated":
  if (response.encrypt_key) {
   Elements.textEncryptKey.setText(response.encrypt_key);
  }
  break;

 case "token_generated":
  if (response.delivery_token) {
   Elements.textToken.setText(response.delivery_token);
  }
  break;

 case "oneliners":
  if (response.oneliners) {
   if (response.oneliners.powershell) Elements.textPSOneliner.setText(response.oneliners.powershell);
   if (response.oneliners.bash) Elements.textBashOneliner.setText(response.oneliners.bash);
   if (response.oneliners.python) Elements.textPyOneliner.setText(response.oneliners.python);
  }
  break;

 case "logs":
  if (response.logs) {
   Elements.tableLogs.setRowCount(0);
   for (var i = 0; i < response.logs.length; i++) {
    addLogEntry(response.logs[i]);
   }
  }
  break;

 case "access_log":
  if (response.entry) {
   addLogEntry(response.entry);
  }
  break;

 case "error":
  ax.show_message("Error", response.message || "Unknown error");
  break;

 case "config_saved":
  if (response.success) {
   ax.show_message("Success", "Configuration saved.");
  }
  break;
 }
}

function fillConfig(cfg) {
 if (cfg.host_bind) {
  var idx = Elements.comboHostBind.findText(cfg.host_bind);
  if (idx >= 0) {
   Elements.comboHostBind.setCurrentIndex(idx);
  }
 }
 if (cfg.port_bind) {
  Elements.spinPortBind.setValue(cfg.port_bind);
 }
 if (cfg.encrypt_key) {
  Elements.textEncryptKey.setText(cfg.encrypt_key);
 }
 if (cfg.delivery_token) {
  Elements.textToken.setText(cfg.delivery_token);
 }
 if (cfg.callback_addresses) {
  Elements.textCallbackAddr.setText(cfg.callback_addresses);
 }
 if (cfg.reconnect_timeout) {
  Elements.textReconnTimeout.setText(cfg.reconnect_timeout);
 }
 if (cfg.reconnect_count) {
  Elements.spinReconnCount.setValue(cfg.reconnect_count);
 }
 if (cfg.ssl) {
  Elements.checkSSL.setChecked(cfg.ssl);
 }
 if (cfg.platforms) {
  Elements.checkWinAmd64.setChecked(cfg.platforms.indexOf("windows/amd64") >= 0);
  Elements.checkWinArm64.setChecked(cfg.platforms.indexOf("windows/arm64") >= 0);
  Elements.checkLinuxAmd64.setChecked(cfg.platforms.indexOf("linux/amd64") >= 0);
  Elements.checkLinuxArm64.setChecked(cfg.platforms.indexOf("linux/arm64") >= 0);
  Elements.checkDarwinAmd64.setChecked(cfg.platforms.indexOf("darwin/amd64") >= 0);
  Elements.checkDarwinArm64.setChecked(cfg.platforms.indexOf("darwin/arm64") >= 0);
 }
}
