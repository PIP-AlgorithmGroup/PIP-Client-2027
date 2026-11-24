# Numeric CMake/PE versions and the displayed prerelease identity have separate formats.
# Keep the suffix here so logs, QML, installers and archive names share one release identity.
set(PIP_VERSION_BASE "0.4.0")
set(PIP_VERSION_PRERELEASE "rc.1")
set(PIP_VERSION_FULL "${PIP_VERSION_BASE}-${PIP_VERSION_PRERELEASE}")
