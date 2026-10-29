# Shared host support; the desktop package and session are managed by the caller.
{ config, lib, pkgs, ... }:
{
  options.programs.zenlink.enable = lib.mkEnableOption "ZenLink phone integration";

  config = lib.mkIf config.programs.zenlink.enable {
    boot.extraModulePackages = [ config.boot.kernelPackages.v4l2loopback ];
    boot.kernelModules = [ "v4l2loopback" ];
    boot.extraModprobeConfig = ''
      options v4l2loopback devices=1 video_nr=10 card_label="ZenLink Phone" exclusive_caps=1
    '';
    # Tag before systemd's seat rules so the active user gets camera access.
    services.udev.packages = [ (pkgs.writeTextDir "lib/udev/rules.d/70-zenlink.rules" ''
      SUBSYSTEM=="video4linux", ATTR{name}=="ZenLink Phone", TAG+="uaccess"
    '') ];
    networking.firewall.allowedTCPPortRanges = [ { from = 1716; to = 1764; } ];
    networking.firewall.allowedUDPPortRanges = [ { from = 1716; to = 1764; } ];
  };
}
