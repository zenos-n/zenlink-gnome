{ pkgs }:

pkgs.stdenv.mkDerivation (finalAttrs: {
  pname = "zenlink-extension";
  version = "0.1.0";
  src = pkgs.lib.cleanSourceWith {
    src = ../.;
    filter = path: type: pkgs.lib.cleanSourceFilter path type &&
      !(builtins.elem (baseNameOf path) [ "_build" "node_modules" "result" ]);
  };

  nativeBuildInputs = with pkgs; [
    meson
    ninja
    pkg-config
    gettext
    glib
    gobject-introspection
    wrapGAppsHook3
    desktop-file-utils
  ];
  buildInputs = with pkgs; [
    gjs
    gtk3
    gsound
    glib-networking
    evolution-data-server
  ];

  mesonFlags = [
    "-Dgnome_shell_libdir=${pkgs.gnome-shell}/lib"
    "-Dopenssl_path=${pkgs.openssl}/bin/openssl"
    "-Dsshadd_path=${pkgs.openssh}/bin/ssh-add"
    "-Dsshkeygen_path=${pkgs.openssh}/bin/ssh-keygen"
    "-Dinstalled_tests=false"
    "-Dsession_bus_services_dir=${placeholder "out"}/share/dbus-1/services"
    "-Dchrome_nmhdir=${placeholder "out"}/etc/opt/chrome/native-messaging-hosts"
    "-Dchromium_nmhdir=${placeholder "out"}/etc/chromium/native-messaging-hosts"
  ];

  postPatch = ''
    patchShebangs installed-tests/prepare-tests.sh

    cat > src/__nix-prepend-search-paths.js <<EOF
    import GIRepository from 'gi://GIRepository';
    '$GI_TYPELIB_PATH'.split(':').filter(Boolean).forEach(path => GIRepository.Repository.dup_default().prepend_search_path(path));
    EOF
    for file in src/extension.js src/prefs.js src/zenlink-preferences.js; do
      sed -i "5i import './__nix-prepend-search-paths.js';" "$file"
    done

    substituteInPlace data/org.zenos.ZenLink.desktop.in \
      --replace-fail 'Exec=gapplication' 'Exec=${pkgs.glib.bin}/bin/gapplication'
    substituteInPlace src/prefs.js src/service/daemon.js \
      --replace-fail "['gjs', '-m'," "['${pkgs.gjs}/bin/gjs', '-m',"
    substituteInPlace src/zenlink-preferences \
      --replace-fail 'exec /usr/bin/env gjs' 'exec ${pkgs.gjs}/bin/gjs'
    substituteInPlace data/config.js.in \
      --replace-fail '@GSETTINGS_SCHEMA_DIR@' \
        "$out/share/gsettings-schemas/${finalAttrs.pname}-${finalAttrs.version}/glib-2.0/schemas"
  '';

  dontWrapGApps = true;
  postFixup = ''
    extensionDir="$out/share/gnome-shell/extensions/zenlink@zenos-n.github.io"
    for file in daemon nativeMessagingHost; do
      wrapGApp "$extensionDir/service/$file.js" \
        --prefix PATH : ${pkgs.lib.makeBinPath [ pkgs.gst_all_1.gstreamer pkgs.pulseaudio ]} \
        --prefix GST_PLUGIN_SYSTEM_PATH_1_0 : ${pkgs.lib.makeSearchPathOutput "lib" "lib/gstreamer-1.0" [ pkgs.gst_all_1.gstreamer pkgs.gst_all_1.gst-plugins-base pkgs.gst_all_1.gst-plugins-good pkgs.gst_all_1.gst-plugins-bad pkgs.pipewire ]}
    done
    mkdir -p "$out/bin"
    makeWrapper "$extensionDir/service/daemon.js" "$out/bin/zenlink"
    makeWrapper "$extensionDir/zenlink-preferences" "$out/bin/zenlink-preferences"
  '';

  passthru.extensionUuid = "zenlink@zenos-n.github.io";

  meta = {
    description = "ZenLink device integration for ZenOS and GNOME";
    license = pkgs.lib.licenses.gpl2Plus;
    platforms = pkgs.lib.platforms.linux;
    mainProgram = "zenlink";
  };
})
