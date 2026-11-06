{
  description = "ZenLink GNOME extension and host media integration";
  inputs.nixpkgs.url = "https://channels.nixos.org/nixos-26.05/nixexprs.tar.zst";
  outputs = { self, nixpkgs, ... }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs { inherit system; };
      package = import ./nix/package.nix { inherit pkgs; };
    in {
      packages.${system} = { default = package; extension = package; };
      nixosModules.default = import ./nix/host.nix { zenlink = self; };
      devShells.${system}.default = pkgs.mkShell {
        inputsFrom = [ package ];
        packages = with pkgs; [ zip python3 pulseaudio
          gst_all_1.gstreamer gst_all_1.gst-plugins-base
          gst_all_1.gst-plugins-good gst_all_1.gst-plugins-bad pipewire ];
      };
      checks.${system}.workspace-units = pkgs.runCommand "zenlink-workspace-units" { nativeBuildInputs = [ pkgs.gjs ]; } ''
        gjs -m ${self}/installed-tests/test-workspace-units.js
        touch "$out"
      '';
      checks.${system}.media-protocol = pkgs.runCommand "zenlink-media-protocol" {
        nativeBuildInputs = [ pkgs.gjs ];
      } ''
        gjs -m ${package.src}/installed-tests/test-media-video.js
        gjs -m ${package.src}/installed-tests/test-media.js
        gjs -m ${package.src}/installed-tests/test-media-window.js ${package.src}/src/service/plugins/media.js
        touch "$out"
      '';
    };
}
