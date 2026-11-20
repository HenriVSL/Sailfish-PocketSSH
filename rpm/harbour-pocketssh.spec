Name:       harbour-pocketssh

Summary:    SSH client with tabs, saved hosts and an xterm-256color terminal
Version:    1.0.0
Release:    1
# The app is MIT; the bundled libssh2 is BSD-3-Clause and libvterm is MIT.
License:    MIT and BSD-3-Clause
URL:        https://github.com/HenriVSL/Sailfish-PocketSSH
Source0:    %{name}-%{version}.tar.bz2
Requires:   sailfishsilica-qt5 >= 0.10.9
BuildRequires:  pkgconfig(sailfishapp) >= 1.0.2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
# OpenSSL 3 (libcrypto.so.3) for the bundled libssh2; rpm adds the runtime
# Requires on libcrypto.so.3 by itself.
BuildRequires:  pkgconfig(openssl) >= 3.0
BuildRequires:  desktop-file-utils

%description
PocketSSH is an SSH client for Sailfish OS: saved hosts, several sessions at
once, an xterm-256color terminal with an extra key row, password and private
key login, and trust-on-first-use host key checks. It bundles libssh2 and
libvterm.


%prep
%setup -q -n %{name}-%{version}

%build
%qmake5
%make_build


%install
%qmake5_install

# The SDK's %%qmake5 sets QMAKE_STRIP=: and the SDK skips rpm's own stripping,
# so the binary would ship with symbols (rpmlint: unstripped-binary-or-object).
# For a debuggable build: sfdev build emu -- --define "pocketssh_keep_symbols 1"
%if 0%{!?pocketssh_keep_symbols:1}
%{__strip} %{buildroot}%{_bindir}/%{name}
%endif

# License texts next to the app (the BSD-3-Clause license of libssh2 asks for
# its notice in binary distributions). %%{_sourcedir} is <source tree>/rpm in
# both in-tree and shadow builds.
install -m 644 %{_sourcedir}/../LICENSE %{_sourcedir}/../THIRD_PARTY_NOTICES.md \
    %{buildroot}%{_datadir}/%{name}/

desktop-file-install --delete-original         --dir %{buildroot}%{_datadir}/applications                %{buildroot}%{_datadir}/applications/*.desktop

%files
%defattr(-,root,root,-)
%{_bindir}/%{name}
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png

# The changelog section is appended by sfdk/mb2 from rpm/harbour-pocketssh.changes.
