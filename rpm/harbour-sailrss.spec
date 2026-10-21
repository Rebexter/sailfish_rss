Name:       harbour-sailrss

Summary:    FreshRSS client for Sailfish OS
Version:    0.1.0
Release:    1
License:    GPL-3.0-or-later
URL:        https://github.com/Rebexter/sailfish_rss
Source0:    %{name}-%{version}.tar.bz2
Requires:   sailfishsilica-qt5 >= 0.10.9
BuildRequires:  pkgconfig(sailfishapp) >= 1.0.2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(Qt5Network)
BuildRequires:  pkgconfig(Qt5Test)
BuildRequires:  qt5-qttools-linguist
BuildRequires:  desktop-file-utils
BuildRequires:  cmake

%description
SailRSS is a native Sailfish OS client for FreshRSS servers,
using the Google Reader compatible API.


%prep
%setup -q -n %{name}-%{version}

%build

%cmake .

%make_build


%check
ctest --output-on-failure


%install
%make_install


desktop-file-install --delete-original       \
  --dir %{buildroot}%{_datadir}/applications             \
   %{buildroot}%{_datadir}/applications/*.desktop

%files
%defattr(-,root,root,-)
%{_bindir}/%{name}
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
