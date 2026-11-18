"""Wrap a verified CMake install tree in a private-prefix Ubuntu amd64 DEB.

Uses only Python's standard library and dpkg-deb. Never installs packages or modifies /usr;
the caller owns the staging/output directories. Repeated calls refuse an existing work tree.
"""

import argparse
from pathlib import Path
import re
import shutil
import subprocess


def package_deb(payload, output, version):
    """Create one DEB under output; fail before copying invalid or redirected input.

    payload is an existing deployed tree, not a CMake build tree. All mutations stay inside
    a newly created output work directory. User settings and logs are never package content.
    """
    payload = payload.resolve(strict=True)
    output = output.resolve()
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+-(alpha|beta|rc)\.[0-9]+", version):
        raise ValueError("Expected a complete prerelease version, e.g. 0.1.0-alpha.1")
    if not (payload / "bin/PIPClient").is_file():
        raise ValueError("Payload does not contain bin/PIPClient")
    if not (payload / "bin/qt.conf").is_file():
        raise ValueError("Payload has not been deployed with Qt runtime configuration")
    if output == payload or payload in output.parents or output in payload.parents:
        raise ValueError("Payload and output must be separate directory trees")
    output.mkdir(parents=True, exist_ok=True)
    work = output / f"deb-work-{version}"
    work.mkdir()
    private = work / "opt/pip-client"
    # Preserve ELF SONAME links instead of duplicating each shared library into several files.
    shutil.copytree(payload, private, symlinks=True)
    for link in private.rglob("*"):
        if link.is_symlink() and private not in link.resolve().parents:
            raise ValueError(f"Payload symlink escapes application tree: {link}")
    # Qt may discover VAAPI in a local build sysroot and copy it as a private dependency.
    # Let apt supply that interface alongside the target machine's DRM/driver stack instead.
    for pattern in ("libva.so*", "libva-drm.so*", "libva-x11.so*", "libdrm.so*"):
        for library in (private / "lib").glob(pattern):
            library.unlink()
    integration = work / "usr/share"
    integration.mkdir(parents=True)
    for name in ("applications", "icons"):
        shutil.move(str(private / "share" / name), str(integration / name))
    desktop = integration / "applications/org.pip.PIPClient.desktop"
    text = desktop.read_text(encoding="utf-8")
    text = text.replace("Exec=PIPClient", "Exec=/opt/pip-client/bin/PIPClient")
    text = text.replace("TryExec=PIPClient", "TryExec=/opt/pip-client/bin/PIPClient")
    desktop.write_text(text, encoding="utf-8", newline="\n")
    executable = work / "usr/bin/pip-client"
    executable.parent.mkdir(parents=True)
    executable.write_text('#!/bin/sh\nexec /opt/pip-client/bin/PIPClient "$@"\n',
                          encoding="utf-8", newline="\n")
    executable.chmod(0o755)
    control = work / "DEBIAN"
    control.mkdir()
    # System display and GPU libraries remain distribution-managed; private Qt/FFmpeg copies
    # are supplied in /opt. apt resolves these names on the declared Ubuntu 22.04 baseline.
    depends = [
        "libc6 (>= 2.35)", "libstdc++6 (>= 11)", "libgcc-s1", "libgl1", "libegl1",
        "libopengl0", "libx11-6", "libx11-xcb1", "libxi6", "libxcb1", "libxcb-cursor0",
        "libxcb-icccm4", "libxcb-image0", "libxcb-keysyms1", "libxcb-randr0",
        "libxcb-render0", "libxcb-render-util0", "libxcb-shape0", "libxcb-shm0",
        "libxcb-sync1", "libxcb-xfixes0", "libxcb-xinerama0", "libxcb-xkb1",
        "libxkbcommon0", "libxkbcommon-x11-0", "libfontconfig1", "libfreetype6",
        "libdbus-1-3", "libglib2.0-0", "libice6", "libsm6", "libwayland-client0",
        "libwayland-cursor0", "libwayland-egl1", "libzstd1", "zlib1g", "libssl3",
        "libgssapi-krb5-2", "libva2", "libva-drm2", "libva-x11-2", "libdrm2",
    ]
    size_kib = sum(path.stat().st_size for path in work.rglob("*")
                   if path.is_file() and not path.is_symlink()) // 1024
    # Debian prereleases sort before the stable version using '~', not SemVer's '-'.
    debian_version = version.replace("-", "~", 1)
    (control / "control").write_text(
        f"Package: pip-client\nVersion: {debian_version}\nArchitecture: amd64\n"
        "Maintainer: PIP Algorithm Group <noreply@github.com>\nSection: net\nPriority: optional\n"
        f"Installed-Size: {size_kib}\nDepends: {', '.join(depends)}\n"
        "Homepage: https://github.com/PIP-AlgorithmGroup/PIP-Client-2027\n"
        "Description: RoboMaster custom terminal preview\n"
        " MQTT session, UDP HEVC video and local diagnostics.\n",
        encoding="utf-8", newline="\n")
    # Source files on Windows-mounted workspaces may appear executable. Normalize regular
    # payload permissions while retaining the real application and shared-library load paths.
    for path in work.rglob("*"):
        if path.is_symlink():
            continue
        if path.is_dir():
            path.chmod(0o755)
        elif path == executable or path == private / "bin/PIPClient":
            path.chmod(0o755)
        else:
            path.chmod(0o644)
    archive = output / f"PIPClient-{version}-Ubuntu-amd64.deb"
    subprocess.run(["dpkg-deb", "--root-owner-group", "--build", str(work), str(archive)],
                   check=True)
    print(archive)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--payload", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", required=True)
    arguments = parser.parse_args()
    package_deb(arguments.payload, arguments.output, arguments.version)
