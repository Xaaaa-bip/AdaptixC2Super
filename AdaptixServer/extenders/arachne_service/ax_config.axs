var metadata = {
 name: "Arachne Bridge",
 description: "Configuration and status panel for the ArachneC2 bridge skeleton"
};

var Elements = {
 dialog: null,
 enabled: null,
 mode: null,
 repoUrl: null,
 dataDir: null,
 notes: null,
 statusTable: null,
 capsText: null,
 limitsText: null
};

function InitService() {
 CreateDialog();
 ax.service_command("arachne", "get_config", {});
 ax.service_command("arachne", "get_status", {});
}

function data_handler(data) {
 try {
 let response = JSON.parse(data);
 switch (response.type) {
 case "config":
 recvConfig(response.data);
 break;
 case "status":
 recvStatus(response.data);
 break;
 case "error":
 ax.show_message("Arachne Bridge", response.message || "Unknown error");
 break;
 }
 } catch (e) {
 ax.log_error("Arachne Bridge: parse error: " + e);
 }
}

function CreateDialog() {
 Elements.enabled = form.create_check("Enable bridge state");

 Elements.mode = form.create_combo();
 Elements.mode.addItems(["bridge", "disabled"]);

 Elements.repoUrl = form.create_textline("");
 Elements.repoUrl.setPlaceholder("https://github.com/portbuster1337/ArachneC2");

 Elements.dataDir = form.create_textline("");
 Elements.dataDir.setPlaceholder("data/arachne");

 Elements.notes = form.create_textmulti("");

 let cfgGrid = form.create_gridlayout();
 cfgGrid.addWidget(Elements.enabled, 0, 0, 1, 2);
 cfgGrid.addWidget(form.create_label("Mode"), 1, 0, 1, 1);
 cfgGrid.addWidget(Elements.mode, 1, 1, 1, 1);
 cfgGrid.addWidget(form.create_label("Repository"), 2, 0, 1, 1);
 cfgGrid.addWidget(Elements.repoUrl, 2, 1, 1, 1);
 cfgGrid.addWidget(form.create_label("Data directory"), 3, 0, 1, 1);
 cfgGrid.addWidget(Elements.dataDir, 3, 1, 1, 1);
 cfgGrid.addWidget(form.create_label("Notes"), 4, 0, 1, 1);
 cfgGrid.addWidget(Elements.notes, 4, 1, 1, 1);

 let cfgPanel = form.create_panel();
 cfgPanel.setLayout(cfgGrid);

 let btnSave = form.create_button("Save");
 let btnRefresh = form.create_button("Refresh");
 let btnEnable = form.create_button("Enable");
 let btnDisable = form.create_button("Disable");

 let buttonLayout = form.create_hlayout();
 buttonLayout.addWidget(btnSave);
 buttonLayout.addWidget(btnEnable);
 buttonLayout.addWidget(btnDisable);
 buttonLayout.addWidget(form.create_hspacer());
 buttonLayout.addWidget(btnRefresh);

 let buttonPanel = form.create_panel();
 buttonPanel.setLayout(buttonLayout);

 Elements.statusTable = form.create_table(["Field", "Value"]);
 Elements.statusTable.setReadOnly(true);
 Elements.statusTable.setSortingEnabled(false);

 Elements.capsText = form.create_textmulti("");
 Elements.capsText.setReadOnly(true);
 Elements.limitsText = form.create_textmulti("");
 Elements.limitsText.setReadOnly(true);

 let statusLayout = form.create_vlayout();
 statusLayout.addWidget(Elements.statusTable);
 statusLayout.addWidget(form.create_label("Capabilities"));
 statusLayout.addWidget(Elements.capsText);
 statusLayout.addWidget(form.create_label("Limitations"));
 statusLayout.addWidget(Elements.limitsText);

 let statusPanel = form.create_panel();
 statusPanel.setLayout(statusLayout);

 let tabs = form.create_tabs();
 tabs.addTab(cfgPanel, "Configuration");
 tabs.addTab(statusPanel, "Status");

 let mainLayout = form.create_vlayout();
 mainLayout.addWidget(buttonPanel);
 mainLayout.addWidget(tabs);

 Elements.dialog = form.create_ext_dialog("Arachne Bridge");
 Elements.dialog.setSize(780, 520);
 Elements.dialog.setLayout(mainLayout);
 Elements.dialog.setButtonsText("Close", "Cancel");

 form.connect(btnSave, "clicked", function() {
 sendConfig();
 });
 form.connect(btnRefresh, "clicked", function() {
 ax.service_command("arachne", "get_config", {});
 ax.service_command("arachne", "get_status", {});
 });
 form.connect(btnEnable, "clicked", function() {
 ax.service_command("arachne", "enable", {});
 });
 form.connect(btnDisable, "clicked", function() {
 ax.service_command("arachne", "disable", {});
 });
}

function recvConfig(config) {
 if (!config) return;
 Elements.enabled.setChecked(config.enabled || false);
 let mode = config.mode || "bridge";
 Elements.mode.setCurrentIndex(mode === "disabled" ? 1 : 0);
 Elements.repoUrl.setText(config.repo_url || "");
 Elements.dataDir.setText(config.data_dir || "");
 Elements.notes.setText(config.notes || "");
}

function sendConfig() {
 let cfg = {
 enabled: Elements.enabled.isChecked(),
 mode: Elements.mode.currentText(),
 repo_url: Elements.repoUrl.text(),
 data_dir: Elements.dataDir.text(),
 notes: Elements.notes.text()
 };
 ax.service_command("arachne", "set_config", cfg);
}

function recvStatus(status) {
 if (!status || !Elements.statusTable) return;
 Elements.statusTable.setRowCount(0);
 addStatusRow("Loaded", status.loaded ? "yes" : "no");
 addStatusRow("Enabled", status.enabled ? "yes" : "no");
 addStatusRow("Mode", status.mode || "");
 addStatusRow("Runtime", status.runtime || "");
 addStatusRow("Repository", status.repo_url || "");
 addStatusRow("Data directory", status.data_dir || "");
 addStatusRow("Started", status.started_at || "");

 Elements.capsText.setText((status.capabilities || []).join("\n"));
 Elements.limitsText.setText((status.limitations || []).join("\n"));
}

function addStatusRow(key, value) {
 Elements.statusTable.addItem([key, String(value)]);
}
