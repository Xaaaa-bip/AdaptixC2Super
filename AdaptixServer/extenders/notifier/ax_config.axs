var metadata = {
 name: "Notifier Settings",
 description: "Settings dialog for event notifications (incl. 钉钉)"
};

var eventOrder = [
 "agent.new",
 "agent.activate",
 "agent.terminate",
 "agent.remove",
 "listener.start",
 "listener.stop",
 "task.complete",
 "download.finish",
 "tunnel.start",
 "tunnel.stop",
 "credentials.add",
 "target.add"
];

var eventLabels = {
 "agent.new": "New Agent",
 "agent.activate": "Agent Activated",
 "agent.terminate": "Agent Terminated",
 "agent.remove": "Agent Removed",
 "listener.start": "Listener Started",
 "listener.stop": "Listener Stopped",
 "task.complete": "Task Completed",
 "download.finish": "Download Finished",
 "tunnel.start": "Tunnel Started",
 "tunnel.stop": "Tunnel Stopped",
 "credentials.add": "Credentials Added",
 "target.add": "Target Added"
};

var eventTemplateVars = {
 "agent.new": "{Id}, {Username}, {Computer}, {Domain}, {ExternalIP}, {InternalIP}, {OsDesc}, {Process}, {Pid}, {Arch}, {Elevated}",
 "agent.activate": "{Id}, {Username}, {Computer}, {Domain}, {ExternalIP}, {InternalIP}, {Elevated}",
 "agent.terminate": "{Id}, {TaskId}",
 "agent.remove": "{Id}, {Username}, {Computer}, {Domain}",
 "listener.start": "{Name}, {Type}",
 "listener.stop": "{Name}, {Type}",
 "task.complete": "{AgentId}, {TaskId}, {CommandLine}, {TaskType}, {Computer}, {Status}, {Message}",
 "download.finish": "{AgentId}, {Computer}, {RemotePath}, {Size}, {FileId}",
 "tunnel.start": "{AgentId}, {TunnelId}, {TunnelType}, {Port}, {Info}",
 "tunnel.stop": "{AgentId}, {TunnelId}, {Port}",
 "credentials.add": "{Username}, {Password}, {Realm}, {Type}, {Tag}, {Storage}, {Host}",
 "target.add": "{Computer}, {Domain}, {Address}, {OsDesk}, {Tag}, {Info}"
};

var Elements = {
 dialog: null,
 checkNotification: null,

 stackServices: null,
 listServices: null,

 stackEvents: null,
 listEvents: null,

 tg_enabled: null,
 tg_bot_token: null,
 tg_chat_ids: null,

 slack_enabled: null,
 slack_webhook_url: null,

 discord_enabled: null,
 discord_webhook_url: null,

 rc_enabled: null,
 rc_webhook_url: null,

 dt_enabled: null,
 dt_webhook_url: null,
 dt_secret: null,

 wh_enabled: null,
 wh_url: null,
 wh_method: null,
 wh_headers: null,
 wh_data: null,

 eventChecks: {},
 eventTemplates: {}
};

function parseHeaders(text) {
 let headers = {};
 let lines = text.split("\n");
 for (let i = 0; i < lines.length; i++) {
 let line = lines[i].trim();
 if (line === "") continue;
 let idx = line.indexOf(":");
 if (idx > 0) {
 let key = line.substring(0, idx).trim();
 let value = line.substring(idx + 1).trim();
 headers[key] = value;
 }
 }
 return headers;
}

function formatHeaders(headers) {
 let lines = [];
 for (let key in headers) {
 lines.push(key + ": " + headers[key]);
 }
 return lines.join("\n");
}

////

function buildTelegramPage() {
 Elements.tg_enabled = form.create_check("Enable Telegram");
 Elements.tg_bot_token = form.create_textline("");
 Elements.tg_bot_token.setPlaceholder("123456789:ABCdefGHIjklMNOpqrsTUVwxyz");
 Elements.tg_chat_ids = form.create_textmulti("");
 Elements.tg_chat_ids.setPlaceholder("1001234567890\n1009876543210\n1009876543656");

 let grid = form.create_gridlayout();
 grid.addWidget(Elements.tg_enabled, 0, 0, 1, 2);
 grid.addWidget(form.create_hline(), 1, 0, 1, 2);
 grid.addWidget(form.create_label("Bot Token:"), 2, 0, 1, 1);
 grid.addWidget(Elements.tg_bot_token, 2, 1, 1, 1);
 grid.addWidget(form.create_label("Chat IDs (one per line):"), 3, 0, 1, 1);
 grid.addWidget(Elements.tg_chat_ids, 3, 1, 1, 1);

 let layout = form.create_vlayout();
 let gp = form.create_panel();
 gp.setLayout(grid);
 layout.addWidget(gp);
 layout.addWidget(form.create_vspacer());

 let panel = form.create_panel();
 panel.setLayout(layout);
 return panel;
}

function buildSlackPage() {
 Elements.slack_enabled = form.create_check("Enable Slack");
 Elements.slack_webhook_url = form.create_textline("");
 Elements.slack_webhook_url.setPlaceholder("https://hooks.slack.com/services/...");

 let grid = form.create_gridlayout();
 grid.addWidget(Elements.slack_enabled, 0, 0, 1, 2);
 grid.addWidget(form.create_hline(), 1, 0, 1, 2);
 grid.addWidget(form.create_label("Webhook URL:"), 2, 0, 1, 1);
 grid.addWidget(Elements.slack_webhook_url, 2, 1, 1, 1);

 let layout = form.create_vlayout();
 let gp = form.create_panel();
 gp.setLayout(grid);
 layout.addWidget(gp);
 layout.addWidget(form.create_vspacer());

 let panel = form.create_panel();
 panel.setLayout(layout);
 return panel;
}

function buildDiscordPage() {
 Elements.discord_enabled = form.create_check("Enable Discord");
 Elements.discord_webhook_url = form.create_textline("");
 Elements.discord_webhook_url.setPlaceholder("https://discord.com/api/webhooks/...");

 let grid = form.create_gridlayout();
 grid.addWidget(Elements.discord_enabled, 0, 0, 1, 2);
 grid.addWidget(form.create_hline(), 1, 0, 1, 2);
 grid.addWidget(form.create_label("Webhook URL:"), 2, 0, 1, 1);
 grid.addWidget(Elements.discord_webhook_url, 2, 1, 1, 1);

 let layout = form.create_vlayout();
 let gp = form.create_panel();
 gp.setLayout(grid);
 layout.addWidget(gp);
 layout.addWidget(form.create_vspacer());

 let panel = form.create_panel();
 panel.setLayout(layout);
 return panel;
}

function buildRocketChatPage() {
 Elements.rc_enabled = form.create_check("Enable Rocket.Chat");
 Elements.rc_webhook_url = form.create_textline("");
 Elements.rc_webhook_url.setPlaceholder("https://rocketchat.example.com/hooks/...");

 let grid = form.create_gridlayout();
 grid.addWidget(Elements.rc_enabled, 0, 0, 1, 2);
 grid.addWidget(form.create_hline(), 1, 0, 1, 2);
 grid.addWidget(form.create_label("Webhook URL:"), 2, 0, 1, 1);
 grid.addWidget(Elements.rc_webhook_url, 2, 1, 1, 1);

 let layout = form.create_vlayout();
 let gp = form.create_panel();
 gp.setLayout(grid);
 layout.addWidget(gp);
 layout.addWidget(form.create_vspacer());

 let panel = form.create_panel();
 panel.setLayout(layout);
 return panel;
}

function buildDingTalkPage() {
 Elements.dt_enabled = form.create_check("启用钉钉机器人");
 Elements.dt_webhook_url = form.create_textline("");
 Elements.dt_webhook_url.setPlaceholder("完整 Webhook 或仅 access_token");
 Elements.dt_secret = form.create_textline("");
 Elements.dt_secret.setPlaceholder("加签密钥 SEC...（未开启加签可留空）");

 let grid = form.create_gridlayout();
 grid.addWidget(Elements.dt_enabled, 0, 0, 1, 2);
 grid.addWidget(form.create_hline(), 1, 0, 1, 2);
 grid.addWidget(form.create_label("Webhook / Token:"), 2, 0, 1, 1);
 grid.addWidget(Elements.dt_webhook_url, 2, 1, 1, 1);
 grid.addWidget(form.create_label("Secret (可选):"), 3, 0, 1, 1);
 grid.addWidget(Elements.dt_secret, 3, 1, 1, 1);

 let layout = form.create_vlayout();
 let gp = form.create_panel();
 gp.setLayout(grid);
 layout.addWidget(gp);
 layout.addWidget(form.create_vspacer());

 let panel = form.create_panel();
 panel.setLayout(layout);
 return panel;
}

function buildWebhookPage() {
 Elements.wh_enabled = form.create_check("Enable Webhook");
 Elements.wh_url = form.create_textline("");
 Elements.wh_url.setPlaceholder("https://example.com/webhook");
 Elements.wh_method = form.create_combo();
 Elements.wh_method.addItems(["GET", "POST", "PUT", "PATCH"]);
 Elements.wh_headers = form.create_textmulti("");
 Elements.wh_headers.setPlaceholder("Content-Type: application/json\nAuthorization: Bearer ...");
 Elements.wh_data = form.create_textmulti("");
 Elements.wh_data.setPlaceholder('{"text": "%data%"}');

 let grid = form.create_gridlayout();
 grid.addWidget(Elements.wh_enabled, 0, 0, 1, 2);
 grid.addWidget(form.create_hline(), 1, 0, 1, 2);
 grid.addWidget(form.create_label("URL:"), 2, 0, 1, 1);
 grid.addWidget(Elements.wh_url, 2, 1, 1, 1);
 grid.addWidget(form.create_label("Method:"), 3, 0, 1, 1);
 grid.addWidget(Elements.wh_method, 3, 1, 1, 1);
 grid.addWidget(form.create_label("Headers:"), 4, 0, 1, 1);
 grid.addWidget(Elements.wh_headers, 4, 1, 1, 1);
 grid.addWidget(form.create_label("Body (%data% = message):"), 5, 0, 1, 1);
 grid.addWidget(Elements.wh_data, 5, 1, 1, 1);

 let layout = form.create_vlayout();
 let gp = form.create_panel();
 gp.setLayout(grid);
 layout.addWidget(gp);
 layout.addWidget(form.create_vspacer());

 let panel = form.create_panel();
 panel.setLayout(layout);
 return panel;
}

///

function recvConfig(config) {
 if (!config)
 return;

 Elements.checkNotification.setChecked(config.enabled || false);

 let services = config.services || {};
 let telegram = services.telegram || {};
 let slack = services.slack || {};
 let discord = services.discord || {};
 let rocketchat = services.rocketchat || {};
 let dingtalk = services.dingtalk || {};
 let webhook = services.webhook || {};

 // Telegram
 Elements.tg_enabled.setChecked(telegram.enabled || false);
 Elements.tg_bot_token.setText(telegram.bot_token || "");
 let tgIds = telegram.chat_ids || [];
 Elements.tg_chat_ids.setText(tgIds.join("\n"));

 // Slack
 Elements.slack_enabled.setChecked(slack.enabled || false);
 Elements.slack_webhook_url.setText(slack.webhook_url || "");

 // Discord
 Elements.discord_enabled.setChecked(discord.enabled || false);
 Elements.discord_webhook_url.setText(discord.webhook_url || "");

 // Rocket.Chat
 Elements.rc_enabled.setChecked(rocketchat.enabled || false);
 Elements.rc_webhook_url.setText(rocketchat.webhook_url || "");

 // DingTalk
 Elements.dt_enabled.setChecked(dingtalk.enabled || false);
 Elements.dt_webhook_url.setText(dingtalk.webhook_url || "");
 Elements.dt_secret.setText(dingtalk.secret || "");

 // Webhook
 Elements.wh_enabled.setChecked(webhook.enabled || false);
 Elements.wh_url.setText(webhook.url || "");
 let method = webhook.method || "POST";
 if (method === "GET") {
 Elements.wh_method.setCurrentIndex(0);
 } else if (method === "PUT") {
 Elements.wh_method.setCurrentIndex(2);
 } else if (method === "PATCH") {
 Elements.wh_method.setCurrentIndex(3);
 } else {
 Elements.wh_method.setCurrentIndex(1);
 }
 Elements.wh_headers.setText(formatHeaders(webhook.headers || {}));
 Elements.wh_data.setText(webhook.data || "");

 // Events
 let events = config.events || {};
 for (let i = 0; i < eventOrder.length; i++) {
 let eventType = eventOrder[i];
 let eventCfg = events[eventType];
 if (eventCfg) {
 Elements.eventChecks[eventType].setChecked(eventCfg.enabled || false);
 Elements.eventTemplates[eventType].setText(eventCfg.template || "");
 } else {
 Elements.eventChecks[eventType].setChecked(false);
 Elements.eventTemplates[eventType].setText("");
 }
 }
}

function sendConfig() {
 let chatIdsRaw = Elements.tg_chat_ids.text().split("\n");
 let chatIds = [];
 for (let i = 0; i < chatIdsRaw.length; i++) {
 let id = chatIdsRaw[i].trim();
 if (id !== "") {
 chatIds.push(id);
 }
 }

 let newConfig = {
 enabled: Elements.checkNotification.isChecked(),
 services: {
 telegram: {
 enabled: Elements.tg_enabled.isChecked(),
 bot_token: Elements.tg_bot_token.text(),
 chat_ids: chatIds
 },
 slack: {
 enabled: Elements.slack_enabled.isChecked(),
 webhook_url: Elements.slack_webhook_url.text()
 },
 discord: {
 enabled: Elements.discord_enabled.isChecked(),
 webhook_url: Elements.discord_webhook_url.text()
 },
 rocketchat: {
 enabled: Elements.rc_enabled.isChecked(),
 webhook_url: Elements.rc_webhook_url.text()
 },
 dingtalk: {
 enabled: Elements.dt_enabled.isChecked(),
 webhook_url: Elements.dt_webhook_url.text(),
 secret: Elements.dt_secret.text()
 },
 webhook: {
 enabled: Elements.wh_enabled.isChecked(),
 url: Elements.wh_url.text(),
 method: Elements.wh_method.currentText(),
 headers: parseHeaders(Elements.wh_headers.text()),
 data: Elements.wh_data.text()
 }
 },
 events: {}
 };

 for (let i = 0; i < eventOrder.length; i++) {
 let eventType = eventOrder[i];
 newConfig.events[eventType] = {
 enabled: Elements.eventChecks[eventType].isChecked(),
 template: Elements.eventTemplates[eventType].text()
 };
 }

 ax.service_command("notifier", "set_config", newConfig);
}


///

function CreateDialog() {
 Elements.checkNotification = form.create_check("Enable notifications");

 let buttonTestMessage = form.create_button("Test Message");
 form.connect(buttonTestMessage, "clicked", function() {
 ax.service_command("notifier", "test_message", {});
 });

 let buttonSave = form.create_button("Save setting");
 form.connect(buttonSave, "clicked", function() {
 sendConfig();
 });

 // --- Services ---

 Elements.stackServices = form.create_stack();
 Elements.stackServices.addPage(buildTelegramPage());
 Elements.stackServices.addPage(buildSlackPage());
 Elements.stackServices.addPage(buildDiscordPage());
 Elements.stackServices.addPage(buildRocketChatPage());
 Elements.stackServices.addPage(buildDingTalkPage());
 Elements.stackServices.addPage(buildWebhookPage());
 Elements.stackServices.setCurrentIndex(0);

 Elements.listServices = form.create_list();
 Elements.listServices.setReadOnly(true);
 Elements.listServices.addItems(["Telegram", "Slack", "Discord", "Rocket.Chat", "钉钉", "Webhook"]);
 Elements.listServices.setCurrentRow(0);
 form.connect(Elements.listServices, "currentRowChanged", function() {
 Elements.stackServices.setCurrentIndex(Elements.listServices.currentRow());
 });

 let service_hlayout = form.create_hlayout();
 service_hlayout.addWidget(Elements.listServices);
 service_hlayout.addWidget(Elements.stackServices);

 let service_panel = form.create_panel();
 service_panel.setLayout(service_hlayout);

 // --- Events ---

 Elements.listEvents = form.create_list();
 Elements.listEvents.setReadOnly(true);
 Elements.stackEvents = form.create_stack();

 for (let i = 0; i < eventOrder.length; i++) {
 let eventType = eventOrder[i];

 let eventLabel = eventLabels[eventType];

 Elements.listEvents.addItem(eventLabel);

 let check = form.create_check("Enable notification");
 Elements.eventChecks[eventType] = check;

 let label_template = form.create_label("Message:");
 let text_template = form.create_textmulti("");
 text_template.setPlaceholder("Use {FieldName} for placeholders...");
 Elements.eventTemplates[eventType] = text_template;

 let label_vars = form.create_label("Templates:");
 let text_vars = form.create_textmulti(eventTemplateVars[eventType]);
 text_vars.setReadOnly(true);

 let event_layout = form.create_gridlayout();
 event_layout.addWidget(check, 0, 0, 1, 2);
 event_layout.addWidget(form.create_hline(), 1, 0, 1, 2);
 event_layout.addWidget(label_template, 2, 0, 1, 1);
 event_layout.addWidget(text_template, 2, 1, 1, 1);
 event_layout.addWidget(label_vars, 3, 0, 1, 1);
 event_layout.addWidget(text_vars, 3, 1, 1, 1);

 let event_panel = form.create_panel();
 event_panel.setLayout(event_layout);
 Elements.stackEvents.addPage(event_panel);
 }

 Elements.stackEvents.setCurrentIndex(0);
 Elements.listEvents.setCurrentRow(0);
 form.connect(Elements.listEvents, "currentRowChanged", function() {
 Elements.stackEvents.setCurrentIndex(Elements.listEvents.currentRow());
 });

 let event_hlayout = form.create_hlayout();
 event_hlayout.addWidget(Elements.listEvents);
 event_hlayout.addWidget(Elements.stackEvents);

 let event_panel = form.create_panel();
 event_panel.setLayout(event_hlayout);

 // --- Main layout ---

 let tabs = form.create_tabs();
 tabs.addTab(service_panel, "Services");
 tabs.addTab(event_panel, "Events");

 let main_layout = form.create_gridlayout();
 main_layout.addWidget(Elements.checkNotification, 0, 0, 1, 1);
 main_layout.addWidget(form.create_hspacer(), 0, 1, 1, 1);
 main_layout.addWidget(buttonTestMessage, 0, 2, 1, 1);
 main_layout.addWidget(form.create_vline(), 0, 3, 1, 1);
 main_layout.addWidget(buttonSave, 0, 4, 1, 1);
 main_layout.addWidget(form.create_hline(), 1, 0, 1, 5);
 main_layout.addWidget(tabs, 2, 0, 1, 5);

 Elements.dialog = form.create_ext_dialog("Notifier settings");
 Elements.dialog.setSize(900, 600);
 Elements.dialog.setLayout(main_layout);
 Elements.dialog.setButtonsText("No save", "Cancel");
}


//////////////////

function InitService() {
 CreateDialog();
 ax.service_command("notifier", "get_config", {});
}

function data_handler(data) {
 let response = JSON.parse(data);
 switch (response.action) {
 case "config":
 case "config_update":
 recvConfig(response);
 break;
 case "save_result":
 if (response.success) {
 ax.show_message("Success", "Configuration saved successfully!");
 } else {
 ax.show_message("Error", "Failed to save: " + (response.error || "Unknown error"));
 }
 break;
 case "test_result":
 if (response.success) {
 ax.show_message("Success", "Test message sent successfully!");
 } else {
 ax.show_message("Error", "Failed to send test message: " + (response.error || "Unknown error"));
 }
 break;
 }
}
