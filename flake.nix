{
  description = "xwmux";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs?ref=nixos-unstable";
  };

  outputs = { self, nixpkgs }:
  let
  systems =  [ "x86_64-linux" ];
  forAllSystems = nixpkgs.lib.genAttrs systems;
  in {
    packages = forAllSystems (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in {
          default = pkgs.stdenv.mkDerivation {
            name = "xwmux";
            src = ./src;
            buildInputs = [ pkgs.libx11 ];
            nativeBuildInputs = [ pkgs.cmake ];
          };
      }
    );
  };
}
