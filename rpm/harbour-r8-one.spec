Name:       harbour-r8-one
Summary:    RPN calculator whose input language is Quadrate
Version:    0.1.0
Release:    1
License:    GPLv3+
URL:        https://github.com/klahr/r8-one-sailfish
Source0:    %{name}-%{version}.tar.bz2
Requires:   sailfishsilica-qt5 >= 0.10.9
BuildRequires:  pkgconfig(sailfishapp) >= 1.0.2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  desktop-file-utils
# QDOS and Quadrate build with meson; build.sh drives it
BuildRequires:  meson
BuildRequires:  ninja
BuildRequires:  python3-base

%description
r8 One is the QDOS calculator on your phone: the same shell, keypad and
display as the homebuilt hardware it was designed for. It is RPN, and its input
language is Quadrate, a stack language, so the keypad that adds two numbers can
also declare a function, loop, and run whole programs.

%prep
%setup -q -n %{name}-%{version}

%build
./build.sh
%qmake5 R8_NATIVE=$PWD/build/native
%make_build

%install
%qmake5_install

desktop-file-install --delete-original \
  --dir %{buildroot}%{_datadir}/applications \
   %{buildroot}%{_datadir}/applications/*.desktop

%files
%defattr(-,root,root,-)
%{_bindir}/%{name}
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png

%changelog
* Sat Sep 26 2026 Joachim Klahr <joachim.klahr@resolutiongames.com> - 0.1.0-1
- First release, on QDOS 0.2.0
