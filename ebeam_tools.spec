Summary:	Calibration tools for ebeam kernel driver
Name:		ebeam_tools
Version:	0.9
Release:	1
Url:		https://sourceforge.net/projects/ebeam
Source0:	https://sourceforge.net/projects/ebeam/files/ebeam_tools-0.9.tar.gz
License:	GPLv3
Group:		System/X11
BuildRequires:	libxi-devel
BuildRequires:	libxext-devel
BuildRequires:	libgsl-devel
BuildRequires:	libxrandr-devel
BuildRequires:	imagemagick

%description
This package provide 2 programs for handling ebeam kernel driver based devices calibration:
   o ebeam_calibrator : the graphical utility to actually calibrate the device
   o ebeam_state : a command-line program to save and restore previous calibration

%prep
%setup -q

%build
%configure2_5x
%make

%install
rm -rf %{buildroot}
%makeinstall_std

mkdir -p %{buildroot}%{_iconsdir}/hicolor/{16x16,32x32,48x48,64x64,128x128}/apps
for i in 16 32 48 64 128;do
    convert -background none -depth 8 -scale "$i"x"$i" res/ebeam_calibrator.svg %{buildroot}%{_iconsdir}/hicolor/"$i"x"$i"/apps/ebeam_calibrator.png
done
mkdir -p %{buildroot}%{_iconsdir}/hicolor/scalable/apps
cp res/ebeam_calibrator.svg %{buildroot}%{_iconsdir}/hicolor/scalable/apps/ebeam_calibrator.svg

%find_lang %{name}

%clean
rm -rf %{buildroot}

%files -f %{name}.lang
%defattr(-,root,root)
%doc AUTHORS ChangeLog COPYING README
%{_bindir}/*
%{_datadir}/applications/*
%{_mandir}/man1/*
%{_iconsdir}/hicolor/*/apps/*
%{_datadir}/pixmaps/*


%changelog
* Wed Aug 01 2012 Yann Cantin <yann.cantin@laposte.net>
+ Revision: 1
- first ebeam_tools rpm

