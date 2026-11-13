Name:       harbour-periscope

# Build a Harbour-compatible package without the libexif dependency with:
#   rpmbuild --define 'harbour 1' ...
%if 0%{?harbour}
%define periscope_qmake_config CONFIG+=harbour
%else
%define periscope_libexif 1
%endif

# >> macros
# << macros

%{!?qtc_qmake:%define qtc_qmake %qmake}
%{!?qtc_qmake5:%define qtc_qmake5 %qmake5}
%{!?qtc_make:%define qtc_make make}
%{?qtc_builddir:%define _builddir %qtc_builddir}
Summary:    Upload pictures to Panoramax
Version:    0.2.1
Release:    1
Group:      Qt/Qt
License:    MIT
URL:        https://scarpino.dev
Source0:    %{name}-%{version}.tar.bz2
Requires:   sailfishsilica-qt5
Requires:   sailfishsecretsdaemon-secretsplugins-default
Requires:   qt5-qtmultimedia
Requires:   qt5-qtdeclarative-import-multimedia
Requires:   qt5-qtdeclarative-import-sensors
Requires:   qt5-qtdeclarative-import-positioning
%if 0%{?periscope_libexif}
Requires:   libexif
%endif
Requires:   libkeepalive
BuildRequires:  pkgconfig(sailfishapp) >= 1.0.2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Gui)
BuildRequires:  pkgconfig(Qt5Multimedia)
BuildRequires:  pkgconfig(Qt5Network)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(sailfishsecrets)
%if 0%{?periscope_libexif}
BuildRequires:  pkgconfig(libexif)
%endif
BuildRequires:  desktop-file-utils

%description
A Sailfish OS client to upload pictures to Panoramax instances.

%if 0%{?_chum}
Title: Periscope
Type: desktop-application
DeveloperName: Andrea Scarpino
Categories:
 - Utility
Custom:
  Repo: https://github.com/ilpianista/harbour-Periscope
PackageIcon: https://raw.githubusercontent.com/ilpianista/harbour-Periscope/master/icons/harbour-periscope.svg
Screenshots:
 - https://raw.githubusercontent.com/ilpianista/harbour-Periscope/master/screenshots/screenshot_1.png
Links:
  Homepage: https://github.com/ilpianista/harbour-Periscope
  Bugtracker: https://github.com/ilpianista/harbour-Periscope/issues
  Donation: https://liberapay.com/ilpianista
%endif


%prep
%setup -q -n %{name}-%{version}

# >> setup
# << setup

%build
# >> build pre
# << build pre

%qtc_qmake5 APP_VERSION=%{version} %{?periscope_qmake_config}

%qtc_make %{?_smp_mflags}

# >> build post
# << build post

%install
rm -rf %{buildroot}
# >> install pre
# << install pre
%qmake5_install

# >> install post
# << install post

desktop-file-install --delete-original       \
  --dir %{buildroot}%{_datadir}/applications             \
   %{buildroot}%{_datadir}/applications/*.desktop

%files
%defattr(-,root,root,-)
%{_bindir}/%{name}
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
# >> files
# << files
