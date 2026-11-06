{ pkgs ? import <nixpkgs> {} }:
pkgs.mkShell {
  GST_PLUGIN_SYSTEM_PATH_1_0 = pkgs.lib.makeSearchPath "lib/gstreamer-1.0" [ pkgs.pipewire pkgs.gst_all_1.gst-plugins-base pkgs.gst_all_1.gst-plugins-good pkgs.gst_all_1.gst-plugins-bad ];
  packages = with pkgs; [ cmake pkg-config gcc openxr-loader openvr libGL libx11 libadwaita glib libsoup_3 json-glib libpng libjpeg gst_all_1.gstreamer gst_all_1.gst-plugins-base gst_all_1.gst-plugins-good gst_all_1.gst-plugins-bad pipewire openssl ];
}
