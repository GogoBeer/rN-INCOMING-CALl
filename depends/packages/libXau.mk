package=libXau
$(package)_version=1.0.9
$(package)_download_path=https://xorg.freedesktop.org/releases/individual/lib/
$(package)_file_name=$(package)-$($(package)_version).tar.bz2
$(package)_sha256_hash=ccf8cbf0dbf676faa2ea0a6d64bcc3b6746064722b606c8c52917ed00dcb73ec
$(package)_dependencies=xproto

# When updating this package, check the default value of
# --disable-xthreads. It is currently enabled.
d