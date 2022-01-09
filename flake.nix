{
  description = "µStreamer - Lightweight and fast MJPG-HTTP streamer";

  # Nixpkgs / NixOS version to use.
  inputs.nixpkgs.url = "github:nixos/nixpkgs/nixos-21.11";

  outputs = { self, nixpkgs }:
    let
      version = builtins.substring 0 8 self.lastModifiedDate;
      supportedSystems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
      nixpkgsFor = forAllSystems (system: import nixpkgs { inherit system; overlays = [ self.overlay ]; });
    in
{
      overlay = final: prev: {
        ustreamer = with final; stdenv.mkDerivation rec {
          name = "ustreamer-${version}";
          src = self;
          buildInputs = [ libbsd libevent libjpeg ];
          installPhase = ''
            mkdir -p $out/bin
            cp ustreamer $out/bin/
          '';
          meta = with lib; {
            homepage = "https://github.com/pikvm/ustreamer";
            description = "Lightweight and fast MJPG-HTTP streamer";
            longDescription = ''
              µStreamer is a lightweight and very quick server to stream MJPG video from
              any V4L2 device to the net. All new browsers have native support of this
              video format, as well as most video players such as mplayer, VLC etc.
              µStreamer is a part of the Pi-KVM project designed to stream VGA and HDMI
              screencast hardware data with the highest resolution and FPS possible.
            '';
            license = licenses.gpl3Plus;
            platforms = platforms.linux;
          };
        };

      };

      packages = forAllSystems (system:
        {
          inherit (nixpkgsFor.${system}) ustreamer;
        });

      defaultPackage = forAllSystems (system: self.packages.${system}.ustreamer);

      nixosModules.ustreamer =
        { pkgs, ... }:
        {
          nixpkgs.overlays = [ self.overlay ];
          environment.systemPackages = [ pkgs.ustreamer ];
          networking.firewall.enable = false;
          systemd.services.ustreamer = {
            description = "ustreamer service";
            wantedBy = [ "multi-user.target" ];
            serviceConfig = {
              DynamicUser = true;
              ExecStart = "${pkgs.ustreamer}/bin/ustreamer --host=0.0.0.0 --port 8000 --device /dev/video9 --device-timeout=8";
              PrivateTmp = true;
              BindReadOnlyPaths = "/dev/video9";
              SupplementaryGroups = [
                "video"
              ];
              Restart = "always";
            };
          };
        };

      # Tests run by 'nix flake check' and by Hydra.
      checks = forAllSystems
        (system:
          with nixpkgsFor.${system};

          lib.optionalAttrs stdenv.isLinux {
            # A VM test of the NixOS module.
            vmTest =
              with import (nixpkgs + "/nixos/lib/testing-python.nix") {
                inherit system;
              };

              makeTest {
                nodes = {
                  client = { ... }: {
                    environment.systemPackages = [ pkgs.curl ];
                  };
                  camera = { config, ... }:
                    let
                      configFile = pkgs.writeText "akvcam-configFile" ''
                        [Cameras]
                        cameras/size = 2

                        cameras/1/type = output
                        cameras/1/mode = mmap, userptr, rw
                        cameras/1/description = Virtual Camera (output device)
                        cameras/1/formats = 2
                        cameras/1/videonr = 7

                        cameras/2/type = capture
                        cameras/2/mode = mmap, rw
                        cameras/2/description = Virtual Camera
                        cameras/2/formats = 1, 2
                        cameras/2/videonr = 9

                        [Connections]
                        connections/size = 1
                        connections/1/connection = 1:2

                        [Formats]
                        formats/size = 2

                        formats/1/format = YUY2
                        formats/1/width = 640
                        formats/1/height = 480
                        formats/1/fps = 30

                        formats/2/format = RGB24, YUY2
                        formats/2/width = 640
                        formats/2/height = 480
                        formats/2/fps = 20/1, 15/2
                      '';
                    in
                    {
                      imports = [ self.nixosModules.ustreamer ];
                      boot.extraModulePackages = [ config.boot.kernelPackages.akvcam ];
                      boot.kernelModules = [ "akvcam" ];
                      boot.extraModprobeConfig = ''
                        options akvcam config_file=${configFile}
                      '';
                    };
                };

                testScript =
                  ''
                    start_all()

                    camera.wait_for_unit("ustreamer.service")
                    camera.wait_for_open_port("8000")

                    client.wait_for_unit("multi-user.target")
                    client.succeed("curl http://camera:8000")
                  '';
              };
          }
        );

    };
}
