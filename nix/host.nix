{ zenlink }:
{ config, lib, pkgs, ... }:
{
  imports = [ ./devices.nix ];

  config = lib.mkIf config.programs.zenlink.enable {
    environment.systemPackages = [
      zenlink.packages.${pkgs.stdenv.hostPlatform.system}.extension
      pkgs.pulseaudio
    ];
  };
}
