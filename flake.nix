{
  description = "xwmux";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs?ref=nixos-unstable";
  };

  outputs = {
    self,
    nixpkgs,
  }: let
    systems = ["x86_64-linux" "aarch64-linux"];
    forAllSystems = nixpkgs.lib.genAttrs systems;
  in {
    packages = forAllSystems (
      system: let
        pkgs = nixpkgs.legacyPackages.${system};
        stdenv = pkgs.stdenv;
      in {
        default = stdenv.mkDerivation {
          name = "xwmux";
          src = ./src;
          buildInputs = [pkgs.libx11];
          nativeBuildInputs = [pkgs.cmake pkgs.makeWrapper];
          postInstall = ''
            for script in \
              $out/bin/xwmux-init-term.sh \
              $out/bin/xwmux-launch-term.sh \
              $out/bin/xwmux-xephyr-test.sh
            do
              wrapProgram "$script" \
                --prefix PATH : ${pkgs.lib.makeBinPath [
                  pkgs.python3
                  pkgs.tmux
                  pkgs.coreutils
                  pkgs.findutils
                  pkgs.gnugrep
                  pkgs.gnused
                  pkgs.gawk
                  pkgs.xdotool
                  pkgs.xwininfo
                  pkgs.xorg.xmessage
                  pkgs.xorg.xorgserver
                ]}
            done
          '';
        };
      }
    );

    devShells = forAllSystems (
      system: let
        pkgs = nixpkgs.legacyPackages.${system};
        stdenv = pkgs.stdenv;
      in {
        default = pkgs.mkShell {
          packages = [
            pkgs.libx11
            pkgs.cmake
            pkgs.clang
            (pkgs.python3.withPackages (ps:
              with ps; [
                flake8
                black
              ]))
            pkgs.pyright
            pkgs.tmux
            pkgs.kitty
            pkgs.alacritty
            pkgs.st
            pkgs.xterm
            pkgs.xdotool
            pkgs.xwininfo
            pkgs.xorg.xmessage
            pkgs.xorg.xorgserver
          ];
        };
      }
    );
  };
}
