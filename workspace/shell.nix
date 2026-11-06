{ pkgs ? import <nixpkgs> {} }:
pkgs.mkShell {
  packages = with pkgs; [ cmake pkg-config gcc openxr-loader openvr libGL libx11 libadwaita glib libsoup_3 json-glib libpng libjpeg gst_all_1.gstreamer gst_all_1.gst-plugins-base gst_all_1.gst-plugins-good gst_all_1.gst-plugins-bad pipewire openssl ];
}
