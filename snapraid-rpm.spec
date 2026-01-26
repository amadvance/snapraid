Name:           snapraid
Summary:        Disk array backup for many large rarely-changed files
Version:        14.0
Release:        3%{?dist}
License:        GPL-3.0-or-later
Group:          Applications/System
URL:            https://www.snapraid.it/
Source0:        https://github.com/amadvance/snapraid/releases/download/v%{version}/snapraid-%{version}.tar.gz
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
rm -rf %{buildroot}
make install DESTDIR=%{buildroot}

%files
%license COPYING
%doc AUTHORS HISTORY README snapraid.conf.example
%{_bindir}/snapraid
%{_mandir}/man1/snapraid.1*
%{_mandir}/man1/snapraid_log.1*
%lang(de) %{_mandir}/de/man1/snapraid.1*
%lang(es) %{_mandir}/es/man1/snapraid.1*
%lang(fr) %{_mandir}/fr/man1/snapraid.1*
%lang(it) %{_mandir}/it/man1/snapraid.1*
%lang(ja) %{_mandir}/ja/man1/snapraid.1*
%lang(ko) %{_mandir}/ko/man1/snapraid.1*
%lang(pl) %{_mandir}/pl/man1/snapraid.1*
%lang(pt) %{_mandir}/pt/man1/snapraid.1*
%lang(ro) %{_mandir}/ro/man1/snapraid.1*
%lang(ru) %{_mandir}/ru/man1/snapraid.1*
%lang(sv) %{_mandir}/sv/man1/snapraid.1*
%lang(uk) %{_mandir}/uk/man1/snapraid.1*
%lang(zh) %{_mandir}/zh/man1/snapraid.1*

%changelog
# Add your changelog entries here
