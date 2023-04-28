Name:           snapraid
Summary:        Disk array backup for many large rarely-changed files
Version:        12.2
Release:        3%{?dist}
License:        GPLv3+
Group:          Applications/System
URL:            http://snapraid.sourceforge.net/
Source:         https://github.com/amadvance/snapraid/releases/download/v%{version}/snapraid-%{version}.tar.gz
BuildRequires:  gcc

%description
SnapRAID is a backup program for disk arrays. It stores parity
information of your data and it's able to recover from up to six disk
failures. SnapRAID is mainly targeted for a home media center, with a
lot of big files that rarely change.

%prep
%setup -q

%build
%configure
make %{?_smp_mflags}

%check
make check

%install
make install DESTDIR=%{buildroot}

%files
%doc COPYING AUTHORS HISTORY README
%{_bindir}/snapraid
%{_mandir}/man1/snapraid.1*

%changelog

