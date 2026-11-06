{ pkgs ? import <nixpkgs> {} }:
pkgs.stdenv.mkDerivation {
  pname = "zenlink-workspace";
  version = "0.1.0";
  src = pkgs.lib.fileset.toSource {
    root = ./.;
    fileset = pkgs.lib.fileset.unions [ ./CMakeLists.txt ./scripts ./environment ./host ./tests/tiles.cpp ./tests/hevc.cpp ./tests/pcvr_settings.cpp ./tests/overlay.cpp ./tests/overlay_gl.cpp ./tests/overlay_xr.cpp ./tests/compositor_guard.cpp ];
  };
  nativeBuildInputs = with pkgs; [ cmake pkg-config makeWrapper wrapGAppsHook4 godot_4 ];
  buildInputs = with pkgs; [ openxr-loader openvr libGL libx11 libadwaita glib libsoup_3 json-glib libpng libjpeg gst_all_1.gstreamer gst_all_1.gst-plugins-base ];
  doCheck = true;
  postInstall = ''
    install -m755 $src/scripts/pcvr $out/bin/zenlink-pcvr
    install -m755 $src/scripts/run-openxr $out/bin/zenlink-run-openxr
    install -m755 $src/scripts/pc-environment $out/bin/zenlink-pc-environment
    mkdir -p $out/share/zenlink
    cp -r $src/environment $out/share/zenlink/environment
    export XDG_DATA_HOME=$TMPDIR/zenlink-data XDG_CONFIG_HOME=$TMPDIR/zenlink-config XDG_CACHE_HOME=$TMPDIR/zenlink-cache
    godot4 --headless --xr-mode off --editor --path $out/share/zenlink/environment --import > $TMPDIR/environment-import.log 2>&1
    cat $TMPDIR/environment-import.log
    if grep -Eq 'SCRIPT ERROR|Parse Error' $TMPDIR/environment-import.log; then exit 1; fi
    timeout 30 godot4 --headless --xr-mode off --path $out/share/zenlink/environment -- --self-test
    for program in zenlink-pcvr zenlink-run-openxr; do
      wrapProgram $out/bin/$program \
        --set ZENLINK_WORKSPACE_BIN_DIR $out/bin \
        --set WORKSPACE_WIVRN_SERVER ${pkgs.wivrn}/bin/wivrn-server \
        --set ZENLINK_PC_ENVIRONMENT_COMMAND $out/bin/zenlink-pc-environment \
        --prefix PATH : ${pkgs.lib.makeBinPath [ pkgs.systemd pkgs.glib pkgs.android-tools pkgs.coreutils pkgs.gnused pkgs.gawk ]}
    done
    wrapProgram $out/bin/zenlink-pc-environment --set ZENLINK_GODOT ${pkgs.godot_4}/bin/godot4 --set ZENLINK_PC_ENVIRONMENT $out/share/zenlink/environment
    wrapProgram $out/bin/zenlink-workspace-configure --set ZENLINK_OPENSSL ${pkgs.openssl}/bin/openssl

    wrapProgram $out/bin/quest-workspace-settings --set-default WORKSPACE_WIVRN_RUNTIME ${pkgs.wivrn}/share/openxr/1/openxr_wivrn.json
    wrapProgram $out/bin/quest-workspace-overlay --prefix GST_PLUGIN_SYSTEM_PATH_1_0 : ${pkgs.lib.makeSearchPath "lib/gstreamer-1.0" (map pkgs.lib.getLib [ pkgs.pipewire pkgs.gst_all_1.gst-plugins-base ])}
    wrapProgram $out/bin/quest-workspace-host --set ZENLINK_PCVR_COMMAND $out/bin/zenlink-pcvr --prefix PATH : ${pkgs.lib.makeBinPath [ pkgs.systemd ]} --prefix GIO_EXTRA_MODULES : ${pkgs.glib-networking}/lib/gio/modules --prefix GST_PLUGIN_SYSTEM_PATH_1_0 : ${pkgs.lib.makeSearchPath "lib/gstreamer-1.0" (map pkgs.lib.getLib [ pkgs.pipewire pkgs.gst_all_1.gstreamer pkgs.gst_all_1.gst-plugins-base pkgs.gst_all_1.gst-plugins-good pkgs.gst_all_1.gst-plugins-bad ])}
  '';
}
