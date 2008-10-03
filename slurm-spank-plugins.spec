##
# $Id: chaos-spankings.spec 7813 2008-09-25 23:08:25Z grondo $
##

#
#  Allow defining --with and --without build options or %_with and %without in .
#    _with    builds option by default unless --without is specified
#    _without builds option iff --with specified
#
%define _with_opt() %{expand:%%{!?_without_%{1}:%%global _with_%{1} 1}}
%define _without_opt() %{expand:%%{?_with_%{1}:%%global _with_%{1} 1}}

#
#  _with helper macro to test for slurm_with_*
#
%define _with() %{expand:%%{?_with_%{1}:1}%%{!?_with_%{1}:0}}

#
#  Build llnl plugins and cpuset by default on chaos systems
#

%if 0%{?chaos}
%_with_opt llnl_plugins
%_with_opt cpuset
%else
%_without_opt llnl_plugins
%_without_opt cpuset
%endif

Name:    
Version:
Release:    

Summary:    SLURM SPANK modules for CHAOS systems
Group:      System Environment/Base
License:    GPL

BuildRoot:  %{_tmppath}/%{name}-%{version}
Source0:    %{name}-%{version}.tgz
Requires: slurm

BuildRequires: slurm-devel bison flex

%if %{_with cpuset}
BuildRequires: libbitmask libcpuset
BuildRequires: pam-devel
%endif

%if %{_with llnl_plugins}
BuildRequires: job
%endif

%description
This package contains a set of SLURM spank plugins which enhance and
extend SLURM functionality for users and administrators.

Currently includes:
 - renice.so :      add --renice option to srun allowing users to set priority 
                    of job
 - system-safe.so : Implement pre-forked system(3) replacement in case MPI
                    implementation doesn't support fork(2).
 - iotrace.so :     Enable tracing of IO calls through LD_PRELOAD trick
 - use-env.so :     Add --use-env flag to srun to override environment
                    variables for job
 - auto-affinity.so: 
                    Try to set CPU affinity on jobs using some kind of 
                    presumably sane defaults. Also adds an --auto-affinity
                    option for tweaking the default behavior.

 - overcommit-memory.so : 
                    Allow users to choose overcommit mode on nodes of
                    their job.

 - pty.so :         Run task 0 of SLURM job under pseudo tty.
 - preserve-env.so: Attempt to preserve exactly the SLURM_* environment
                    variables in remote tasks. Meant to be used like:
		     salloc -n100 srun --preserve-slurm-env -n1 -N1 --pty bash


%if %{_with llnl_plugins}
%package  llnl
Summary:  SLURM spank plugins LLNL-only
Group:    System Environment/Base
Requires: slurm job
%description llnl
The set of SLURM SPANK plugins that will only run on LLNL systems.
Includes:
 - oom-detect.so : Detect tasks killed by OOM killer.
%endif


%if %{_with cpuset}
%package  cpuset
Summary:  Cpuset spank plugin for slurm.
Group:    System Environment/Base
Requires: libbitmask libcpuset slurm pam

%description cpuset
This package contains a SLURM spank plugin for enabling
the use of cpusets to constrain CPU use of jobs on nodes to
the number of CPUs allocated. This plugin is specifically
designed for systems sharing nodes and using CPU scheduling
(i.e.  using the sched/cons_res plugin). Most importantly the
plugin will be harmful when overallocating CPUs on nodes. The
plugin is enabled by adding the line:

 required cpuset.so [options]

to /etc/slurm/plugstack.conf.

A PAM module - pam_slurm_cpuset.so - is also provided for
constraining user logins in a similar fashion. For more
information see the slurm-cpuset(8) man page provided with
this package.
%endif

%prep
%setup

%build
make \
  %{?_with_llnl_plugins:BUILD_LLNL_ONLY=1} \
  %{?_with_cpuset:BUILD_CPUSET=1} \
  CFLAGS="$RPM_OPT_FLAGS" 

%install
rm -rf "$RPM_BUILD_ROOT"
mkdir -p "$RPM_BUILD_ROOT"

make \
  LIBNAME=%{_lib} \
  LIBDIR=%{_libdir} \
  BINDIR=%{_bindir} \
  SBINDIR=/sbin \
  LIBEXECDIR=%{_libexecdir} \
  DESTDIR="$RPM_BUILD_ROOT" \
  %{?_with_llnl_plugins:BUILD_LLNL_ONLY=1} \
  %{?_with_cpuset:BUILD_CPUSET=1} \
  install

# slurm-cpuset init script
install -D -m0755 cpuset/cpuset.init \
		$RPM_BUILD_ROOT/%{_sysconfdir}/init.d/slurm-cpuset

# create /etc/slurm/plugstack.d directory
mkdir -p $RPM_BUILD_ROOT/%{_sysconfdir}/slurm/plugstack.conf.d

# create entry for preserve-env.so
echo " required  preserve-env.so" > \
     $RPM_BUILD_ROOT/%{_sysconfdir}/slurm/plugstack.conf.d/99-preserve-env

%clean
rm -rf "$RPM_BUILD_ROOT"

%post cpuset
if [ -x /sbin/chkconfig ]; then /sbin/chkconfig --add slurm-cpuset; fi

%preun cpuset
if [ "$1" = 0 ]; then
  if [ -x /sbin/chkconfig ]; then /sbin/chkconfig --del slurm-cpuset; fi
fi

%files 
%defattr(-,root,root,0755)
%doc NEWS ChangeLog README.use-env
%{_libdir}/slurm/renice.so
%{_libdir}/slurm/oom-detect.so
%{_libdir}/slurm/system-safe.so
%{_libdir}/slurm/iotrace.so
%{_libdir}/slurm/tmpdir.so
%{_libdir}/slurm/use-env.so
%{_libdir}/slurm/overcommit-memory.so
%{_libdir}/slurm/auto-affinity.so
%{_libdir}/slurm/preserve-env.so
%{_libdir}/slurm/pty.so 
%{_libdir}/slurm/addr-no-randomize.so
%{_libdir}/system-safe-preload.so
%{_libexecdir}/%{name}/overcommit-util
%dir %attr(0755,root,root) %{_sysconfdir}/slurm/plugstack.conf.d
%config(noreplace) %{_sysconfdir}/slurm/plugstack.conf.d/*

%files llnl
%defattr(-,root,root,0755)
%doc NEWS ChangeLog
%{_libdir}/slurm/oom-detect.so

%files cpuset
%defattr(-,root,root,0755)
%doc NEWS ChangeLog cpuset/README
%{_sysconfdir}/init.d/slurm-cpuset
%{_libdir}/slurm/cpuset.so
/%{_lib}/security/pam_slurm_cpuset.so
/sbin/cpuset_release_agent
%{_mandir}/man1/use-cpusets.*
%{_mandir}/man8/pam_slurm_cpuset.*
%{_mandir}/man8/slurm-cpuset.*

