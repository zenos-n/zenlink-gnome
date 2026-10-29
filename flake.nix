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
        packages = [ pkgs.zip pkgs.python3 pkgs.pulseaudio ];
      };
      checks.${system}.media-protocol = pkgs.runCommand "zenlink-media-protocol" {
        nativeBuildInputs = [ pkgs.gjs ];
      } ''
        gjs -m ${package.src}/installed-tests/test-media.js
        touch "$out"
      '';
    };
}
