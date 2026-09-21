/// Gopher TCP listener

function ListenerUI(mode_create)
{
    let labelHost = form.create_label("Host & port (Bind):");
    let comboHostBind = form.create_combo();
    comboHostBind.setEnabled(mode_create)
    comboHostBind.clear();
    let addrs = ax.interfaces();
    for (let item of addrs) { comboHostBind.addItem(item); }
    let spinPort = form.create_spin();
    spinPort.setRange(1, 65535);
    spinPort.setValue(4444);
    spinPort.setEnabled(mode_create)

    let labelCallback = form.create_label("Callback addresses:");
    let texteditCallback = form.create_textmulti();
    texteditCallback.setPlaceholder("192.168.1.1:4444\nserver2.com:5555");

    let labelTimeout = form.create_label("Timeout (sec):");
    let spinTimeout = form.create_spin();
    spinTimeout.setRange(1, 1000000);
    spinTimeout.setValue(10);

    let labelBanner = form.create_label("TCP banner:");
    let texteditBanner = form.create_textmulti("");

    let labelAnswer = form.create_label("Error answer:");
    let texteditAnswer = form.create_textmulti("Connection error...\n");

    let labelEncryptKey = form.create_label("Encryption key:");
    let textlineEncryptKey = form.create_textline(ax.random_string(32, "hex"));
    textlineEncryptKey.setEnabled(mode_create)
    let buttonEncryptKey = form.create_button("Generate");
    buttonEncryptKey.setEnabled(mode_create)

    let labelProto = form.create_label("Transport protocol:");
    let comboProto = form.create_combo();
    comboProto.setEnabled(mode_create)
    comboProto.clear();
    comboProto.addItem("tcp");
    comboProto.addItem("libp2p");

    let labelL2pProto = form.create_label("libp2p protocol id:");
    let textlineL2pProto = form.create_textline("/gopher/1.0.0");

    let labelL2pKey = form.create_label("libp2p privkey (b64):");
    let textlineL2pKey = form.create_textline("");

    let labelL2pRelays = form.create_label("libp2p relays (隐藏 C2):");
    let texteditL2pRelays = form.create_textmulti("");
    texteditL2pRelays.setPlaceholder("/ip4/<relay公网IP>/tcp/4446/p2p/<relayPeerID>\n一行一条；填了它 ⇒ agent 只拨 circuit 地址，C2 真实 IP:端口不进载荷");

    let labelL2pAddrs = form.create_label("libp2p addrs (agent 拨号地址):");
    let texteditL2pAddrs = form.create_textmulti("");
    texteditL2pAddrs.setPlaceholder("留空 ⇒ 由 l2p_relays 自动派生为\n<relay>/p2p-circuit/p2p/<本 listener 的 PeerID>\n填了 ⇒ 覆盖回连地址（高级：自建 circuit / 多跳）");

    let labelL2pDialTimeout = form.create_label("libp2p dial timeout (秒):");
    let spinL2pDialTimeout = form.create_spin();
    spinL2pDialTimeout.setRange(0, 3600);
    spinL2pDialTimeout.setValue(0);

    let checkMtls = form.create_check("Use mTLS");
    checkMtls.setEnabled(mode_create)

    let howButton = form.create_button("How generate?");

    let caCertSelector = form.create_selector_file();
    caCertSelector.setPlaceholder("CA cert");
    caCertSelector.setEnabled(false);

    let srvCertSelector = form.create_selector_file();
    srvCertSelector.setPlaceholder("Server cert");
    srvCertSelector.setEnabled(false);

    let srvKeySelector = form.create_selector_file();
    srvKeySelector.setPlaceholder("Server key");
    srvKeySelector.setEnabled(false);

    let clientCertSelector = form.create_selector_file();
    clientCertSelector.setPlaceholder("Client cert");
    clientCertSelector.setEnabled(false);

    let clientKeySelector = form.create_selector_file();
    clientKeySelector.setPlaceholder("Client key");
    clientKeySelector.setEnabled(false);

    let layout = form.create_gridlayout();
    layout.addWidget(labelHost,          0, 0, 1, 1);
    layout.addWidget(comboHostBind,      0, 1, 1, 2);
    layout.addWidget(spinPort,           0, 3, 1, 1);
    layout.addWidget(labelCallback,      1, 0, 1, 1);
    layout.addWidget(texteditCallback,   1, 1, 1, 3);
    layout.addWidget(labelTimeout,       2, 0, 1, 1);
    layout.addWidget(spinTimeout,        2, 1, 1, 3);
    layout.addWidget(labelBanner,        3, 0, 1, 1);
    layout.addWidget(texteditBanner,     3, 1, 1, 3);
    layout.addWidget(labelAnswer,        4, 0, 1, 1);
    layout.addWidget(texteditAnswer,     4, 1, 1, 3);
    layout.addWidget(labelEncryptKey,    5, 0, 1, 1);
    layout.addWidget(textlineEncryptKey, 5, 1, 1, 2);
    layout.addWidget(buttonEncryptKey,   5, 3, 1, 1);
    layout.addWidget(labelProto,         9, 0, 1, 1);
    layout.addWidget(comboProto,         9, 1, 1, 3);
    layout.addWidget(labelL2pProto,     10, 0, 1, 1);
    layout.addWidget(textlineL2pProto,  10, 1, 1, 3);
    layout.addWidget(labelL2pKey,       11, 0, 1, 1);
    layout.addWidget(textlineL2pKey,    11, 1, 1, 3);
    layout.addWidget(labelL2pRelays,    12, 0, 1, 1);
    layout.addWidget(texteditL2pRelays, 12, 1, 1, 3);
    layout.addWidget(labelL2pAddrs,     13, 0, 1, 1);
    layout.addWidget(texteditL2pAddrs,  13, 1, 1, 3);
    layout.addWidget(labelL2pDialTimeout, 14, 0, 1, 1);
    layout.addWidget(spinL2pDialTimeout,  14, 1, 1, 3);
    layout.addWidget(checkMtls,          6, 0, 1, 1);
    layout.addWidget(howButton,          6, 1, 1, 1);
    layout.addWidget(caCertSelector,     6, 2, 1, 2);
    layout.addWidget(srvKeySelector,     7, 0, 1, 2);
    layout.addWidget(srvCertSelector,    7, 2, 1, 2);
    layout.addWidget(clientKeySelector,  8, 0, 1, 2);
    layout.addWidget(clientCertSelector, 8, 2, 1, 2);

    form.connect(buttonEncryptKey, "clicked", function() { textlineEncryptKey.setText( ax.random_string(32, "hex") ); });

    form.connect(checkMtls, "stateChanged", function() {
        if(caCertSelector.getEnabled()) {
            caCertSelector.setEnabled(false);
            srvCertSelector.setEnabled(false);
            srvKeySelector.setEnabled(false);
            clientCertSelector.setEnabled(false);
            clientKeySelector.setEnabled(false);
        } else {
            caCertSelector.setEnabled(true);
            srvCertSelector.setEnabled(true);
            srvKeySelector.setEnabled(true);
            clientCertSelector.setEnabled(true);
            clientKeySelector.setEnabled(true);
        }
    });

    form.connect(howButton, "clicked", function() {
        let dialog = form.create_dialog("Generate mTLS certificates");

        let infoText = form.create_textmulti();
        infoText.setReadOnly(true);
        infoText.appendText("# CA cert");
        infoText.appendText("openssl genrsa -out ca.key 2048");
        infoText.appendText("openssl req -x509 -new -nodes -key ca.key -sha256 -days 3650 -out ca.crt -subj \"/CN=Test CA\"\n");
        infoText.appendText("# server cert and key");
        infoText.appendText("openssl genrsa -out server.key 2048");
        infoText.appendText("openssl req -new -key server.key -out server.csr -subj \"/CN=localhost\"");
        infoText.appendText("openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial -out server.crt -days 365 -sha256\n");
        infoText.appendText("# client cert and key");
        infoText.appendText("openssl genrsa -out client.key 2048");
        infoText.appendText("openssl req -new -key client.key -out client.csr -subj \"/CN=client\"");
        infoText.appendText("openssl x509 -req -in client.csr -CA ca.crt -CAkey ca.key -CAcreateserial -out client.crt -days 365 -sha256");

        let layout = form.create_vlayout();
        layout.addWidget(infoText);

        dialog.setLayout(layout);
        dialog.setSize(1100, 340);
        dialog.exec()
    });

    // ★ Phase5 批次16：libp2p 组的**开关语义**。
    //   protocol=tcp 时这些键不参与任何逻辑（validConfig 的 tcp 分支根本不看它们），
    //   若仍可编辑就是"填了以为生效"的经典坑 ⇒ 整组随 protocol 联动。
    //   初始态从 combo 当前文本取（编辑既有 libp2p listener 时不会被错误禁用）。
    let l2pGroup = [labelL2pProto, textlineL2pProto, labelL2pKey, textlineL2pKey,
                    labelL2pRelays, texteditL2pRelays, labelL2pAddrs, texteditL2pAddrs,
                    labelL2pDialTimeout, spinL2pDialTimeout];
    function l2pSwitch(on) { for (let w of l2pGroup) { w.setEnabled(on); } }
    form.connect(comboProto, "currentTextChanged", function(text) { l2pSwitch(text === "libp2p"); });
    l2pSwitch(comboProto.currentText() === "libp2p");

    let container = form.create_container()
    container.put("host_bind", comboHostBind)
    container.put("port_bind", spinPort)
    container.put("callback_addresses", texteditCallback)
    container.put("timeout", spinTimeout)
    container.put("tcp_banner", texteditBanner)
    container.put("error_answer", texteditAnswer)
    container.put("encrypt_key", textlineEncryptKey);
    container.put("ssl", checkMtls)
    container.put("ca_cert", caCertSelector)
    container.put("server_cert", srvCertSelector)
    container.put("server_key", srvKeySelector)
    container.put("client_cert", clientCertSelector)
    container.put("client_key", clientKeySelector)
    // ★ Phase5：tcp 或 libp2p。存进 config JSON 的 "protocol" 键，
    //   由 TransportConfig.Protocol 接住，并在 GetProfile() 里流到 teamserver。
    //   ★★ 本组全部键都是 transport.Config 的字段 ⇒ GetProfile() 把整份 Config
    //   编码给 teamserver，agent 侧插件按 listenerMap[<同名键>] 取值
    //   （gopher_agent/pl_main.go: l2p_addrs 覆盖 callback_addresses；l2p_protocol /
    //   l2p_dial_timeout 直接进 Profile）⇒ **UI 改了就等于载荷改了**，无需再改 Profile 结构。
    //   l2p_peer_id 由 listener 的私钥派生（自报），l2p_privkey 不进 profile（防拨自己+防外泄）。
    container.put("protocol", comboProto)
    container.put("l2p_protocol", textlineL2pProto)
    container.put("l2p_privkey", textlineL2pKey)
    container.put("l2p_relays", texteditL2pRelays)
    container.put("l2p_addrs", texteditL2pAddrs)
    container.put("l2p_dial_timeout", spinL2pDialTimeout)

    let panel = form.create_panel()
    panel.setLayout(layout)

    return {
        ui_panel: panel,
        ui_container: container,
        ui_height: 800,
        ui_width: 650
    }
}
