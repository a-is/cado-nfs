#!/usr/bin/perl -w

# General script for factoring integers with Cado-NFS.
#
# usage:
#    cadofactor.pl param=<paramfile> wdir=<...> ...
# Parameters passed in arguments *after* para=... will override choices
# that are made in paramfile.

# If the parameter n=<n> is given, then n is factored. Otherwise, it is
# taken from stdin.

# NB: all the shell commands that are run by this script are reported in
# the $wdir/$name.cmd file, with full arguments, so that it is easy to
# reproduce part of the computation.


use strict;
use warnings;
use File::Copy;
use POSIX qw(ceil floor);

# Default parameters

my $default_param = {
    parallel=>'false',
    degree=>5,
    bmin=>1,
    bmax=>20,
    e=>1e8,
    rlim=>8000000,
    alim=>8000000,
    lpbr=>29,
    lpba=>29,
    mfbr=>58,
    mfba=>58,
    rlambda=>2.3,
    alambda=>2.3,
    I=>13,
    excess=>100,
    qmin=>12000000,
    qrange=>1000000,
    checkrange=>1000000,
    sievenice=>19,
    prune=>1.0,
    merge=>1000000,
    keep=>160,
    maxlevel=>15,
    cwmax=>200,
    rwmax=>200,
    ratio=>1.5,
    bwmt=>2,
    linalg=>'bw',
    nkermax=>30,
    nchar=>50,
};


sub splitpath {
    # Returns dirname and basename. 
    $_[0] =~ m{^(?:(.*)/)?(.*)$};
    my $dir=defined($1) ? $1 : ".";
    my $base=$2;
    if ($dir eq '.') {
        $dir = `pwd`;
        chomp $dir;
    }
    return ($dir, $base);
}

sub read_param {
    my @args = @_;
    my $param = $default_param;
    while (scalar @args) {
        $_ = $args[0];
        if (/^parallel=(.*)$/) { $param->{parallel}=$1; shift @args; next; }
        if (/^degree=(.*)$/)   { $param->{degree}=$1; shift @args; next; }
        if (/^bmin=(.*)$/)     { $param->{bmin}=$1; shift @args; next; }
        if (/^bmax=(.*)$/)     { $param->{bmax}=$1; shift @args; next; }
        if (/^e=(.*)$/)        { $param->{e}=$1; shift @args; next; }
        if (/^rlim=(.*)$/)     { $param->{rlim}=$1; shift @args; next; }
        if (/^alim=(.*)$/)     { $param->{alim}=$1; shift @args; next; }
        if (/^lpbr=(.*)$/)     { $param->{lpbr}=$1; shift @args; next; }
        if (/^lpba=(.*)$/)     { $param->{lpba}=$1; shift @args; next; }
        if (/^mfbr=(.*)$/)     { $param->{mfbr}=$1; shift @args; next; }
        if (/^mfba=(.*)$/)     { $param->{mfba}=$1; shift @args; next; }
        if (/^rlambda=(.*)$/)  { $param->{rlambda}=$1; shift @args; next; }
        if (/^alambda=(.*)$/)  { $param->{alambda}=$1; shift @args; next; }
        if (/^I=(.*)$/)        { $param->{I}=$1; shift @args; next; }
        if (/^excess=(.*)$/)   { $param->{excess}=$1; shift @args; next; }
        if (/^qmin=(.*)$/)     { $param->{qmin}=$1; shift @args; next; }
        if (/^qrange=(.*)$/)   { $param->{qrange}=$1; shift @args; next; }
        if (/^checkrange=(.*)$/){ $param->{checkrange}=$1; shift @args; next; }
        if (/^sievenice=(.*)$/){ $param->{sievenice}=$1; shift @args; next; }
        if (/^prune=(.*)$/)    { $param->{prune}=$1; shift @args; next; }
        if (/^merge=(.*)$/)    { $param->{merge}=$1; shift @args; next; }
        if (/^keep=(.*)$/)     { $param->{keep}=$1; shift @args; next; }
        if (/^maxlevel=(.*)$/) { $param->{maxlevel}=$1; shift @args; next; }
        if (/^cwmax=(.*)$/)    { $param->{cwmax}=$1; shift @args; next; }
        if (/^rwmax=(.*)$/)    { $param->{rwmax}=$1; shift @args; next; }
        if (/^ratio=(.*)$/)    { $param->{ratio}=$1; shift @args; next; }
        if (/^bwstrat=(.*)$/)  { $param->{bwstrat}=$1; shift @args; next; }
        if (/^bwmt=(.*)$/)     { $param->{bwmt}=$1; shift @args; next; }
        if (/^linalg=(.*)$/)   { $param->{linalg}=$1; shift @args; next; }
        if (/^nkermax=(.*)$/)  { $param->{nkermax}=$1; shift @args; next; }
        if (/^nchar=(.*)$/)    { $param->{nchar}=$1; shift @args; next; }
        if (/^wdir=(.*)$/)     { $param->{wdir}=$1; shift @args; next; }
        if (/^name=(.*)$/)     { $param->{name}=$1; shift @args; next; }
        if (/^cadodir=(.*)$/)  { $param->{cadodir}=$1; shift @args; next; }
        if (/^machines=(.*)$/) { $param->{machines}=$1; shift @args; next; }
        if (/^n=(.*)$/) { $param->{n}=$1; shift @args; next; }
        if (/^param=(.*)$/) { 
            my $file = $1; shift @args;
            open FILE, "<$file" or die "$file: $!\n";
            my @newargs;
            my $line;
            while (<FILE>) {
                $line = $_;
                if (/^\s*#/) { next; }
                if (/^\s*$/) { next; }
                if (/^\s*(\w*)=([a-zA-Z0-9_\.\/\$]*)\s*(#|$)/) {
                    push @newargs, "$1=$2";
                    next;
                }
                die "Could not parse line: $_ in file $file\n";
            }
            close FILE;
            @args = (@newargs, @args);
            next;
        }
        die "Unknwon argument: $_\n";
    }
    # checking mandatory parameters:
    if (!$param->{wdir}) { die "I need a wdir argument!\n"; }
    if (!$param->{name}) { die "I need a name argument!\n"; }
    if (!$param->{machines} && $param->{parallel} eq "true") {
        die "With parallel, I need a machines argument!\n"; }
    if (!$param->{cadodir}) {
        print STDERR "Warning: taking current script's basedir as cadodir\n";
        ($param->{cadodir}) =  splitpath($0);
    }
    return $param;
}

# Todo: print w.r.t some order...
sub print_param {
    my $param = shift @_;
    my $fh;
    if ($_[0]) {
        $fh = $_[0];
    }
    my $k;
    my $v;
    while ( ($k,$v) = each %$param ) {
        if ($fh) {
            print $fh "$k=$v\n";
        } else {
            print "$k=$v\n";
        }
    }
}

# Run $cmd, and echo it to $file.
# In case the return code of the command is not 0, we die, unless 
# a 3rd argument 'no_kill' is passed.
sub my_system {
    my $cmd = shift @_;
    my $file = shift @_;
    my $nokill=0;
    if ($_[0] && $_[0] eq 'no_kill') {
        $nokill=1;
    }
    open FH, ">> $file";
    print FH $cmd . "\n";
    close FH;
    my $ret = system($cmd);
    if ($ret != 0 && !$nokill) {
        die "$cmd exited unexpectedly: $!\n";
    }
}

# Execute the given $cmd using backticks, allowing $timeout time for it
# to finish. 
# Returns a pair ( $t, $ret )
#   where $t is a 0 or 1 depending whether the timout was reached (0
#   means timeout, 1 means success).
#   and $ret is the returned value (empty string if timeout).
#
# For timeout, we use the alarm mechanism. See perldoc -f alarm.
sub my_system_timeout {
    my ($cmd, $timeout) = @_;
    my $ret = "";
    eval {
        local $SIG{ALRM} = sub { die "alarm\n" }; # NB: \n required
        alarm $timeout;
        $ret = `$cmd`;
        alarm 0;
    };
    if ($@) {
        die unless $@ eq "alarm\n";   # propagate unexpected errors
        print "WARNING! The command $cmd timed out.\n";
        return (0, "");
    }
    else {
        return (1, $ret);
    }
}


# returns -1, 0 or 1, depending is the last modification date of 
# $f1 is less, equal or more than the modif date of $f2.
sub cmp_filedate {
    my ($f1, $f2) = @_;
    my $d1 = (stat($f1))[9];
    my $d2 = (stat($f2))[9];
    if ($d1 < $d2) {
        return -1;
    } elsif ($d1 == $d2) {
        return 0;
    } else {
        return 1;
    }
}

sub polyselect {
    my $param = shift @_;
    my $prefix = $param->{wdir} . "/" . $param->{name};
    my $b;
    my $bestb=0;
    my $Emin;
    for ($b = $param->{bmin}; $b <= $param->{bmax}; $b++) {
        print "Running polynomial selection with b=$b...\n";
        my $effort=$param->{e};
        my $degree=$param->{degree};
        if (-f "$prefix.poly.$b") {
            print "Result file is already there. Skip the computation!\n";
        } else {
            my $cmd = $param->{cadodir}."/polyselect/polyselect " .
              "-b $b -e $effort -degree $degree < $prefix.n";
            my_system($cmd . "> $prefix.poly.$b", "$prefix.cmd");  
        }
        open FILE, "< $prefix.poly.$b";
        my @lines = readline(FILE);
        close FILE;
        $_ = $lines[2];
        if (/logmu\+alpha=([0-9]*.[0-9]*)/) {
            my $E = $1;
            if ((!$Emin) || $E < $Emin) {
                $Emin = $E;
                $bestb = $b;
            }
        } else {
            die "Can not parse output of poly.$b for geting logmu+alpha\n";
        }
    }
    # choose best.
    # For the moment, we do it according to logmu+alpha
    print "Best polynomial is for b = $bestb\n";
    copy("$prefix.poly.$bestb" , "$prefix.poly");
}


sub try_singleton {
    my $param = shift @_;
    my $nrels = shift @_;
    my $prefix = $param->{wdir} . "/" . $param->{name};

    # Remove duplicates
    print "Removing duplicates...\n";
    my $cmd = $param->{cadodir} .
      "/linalg/duplicates -nrels $nrels $prefix.rels.* > $prefix.nodup 2> $prefix.duplicates.stderr";
    my_system($cmd, "$prefix.cmd");
    my @grouik = split(/ /, `tail -1 $prefix.duplicates.stderr`);
    my $nrels_dup = $grouik[(scalar @grouik)-1];
    chomp $nrels_dup;
    print "We have $nrels_dup relations left after removing duplicates\n";

    # Singleton removal
    print "Singleton removal...\n";
    $cmd = $param->{cadodir} .
      "/linalg/purge -poly $prefix.poly -nrels $nrels_dup $prefix.nodup > $prefix.purged 2> $prefix.purge.stderr";
    my_system($cmd, "$prefix.cmd", "no_kill");
    if (-z "$prefix.purged") {
        printf "Not enough relations!\n";
        return 0;
    } else {
        open FH, "< $prefix.purged";
        my $line = <FH>;
        my ($nrows, $ncols) = split(/ /, $line);
        close FH;
        print "Nrows = $nrows , Ncols = $ncols";
        print "Excess = " . ($nrows - $ncols) . "\n";
        if ($nrows - $ncols <= 0) {
            print "Not enough relations!\n";
            return 0;
        }
    }
    return 1;
}

# Read a status file:
# Each line is of the form
#   hostname q0 q1
# Any empty line is ignored.
# any line that starts with # is ignored
sub read_status_file {
    my $param = shift @_;
    my $prefix = $param->{wdir} . "/" . $param->{name};
    my $statfile = "$prefix.status";
    if (! -f $statfile) {
        print "No status file found. Creating an empty one...\n";
        open GRR, ">$statfile" or die "$statfile: $!\n";
        close GRR;
        return ();
    }
    my @status;
    open SF, "<$statfile" or die "$statfile: $!\n";
    while (<SF>) {
        if (/^\s*\n$/) { next; }
        if (/^#/) { next; }
        if (/^\s*(\w*)\s+(\d+)\s+(\d+)/) {
            push @status, [$1, $2, $3];
            next;
        }
        die "Could not parse status file: $_\n";
    }
    close SF;
    return @status;
}

sub write_status_file {
    my $param = shift @_;
    my $status = shift @_;
    my $prefix = $param->{wdir} . "/" . $param->{name};
    my $statfile = "$prefix.status";
    open GRR, ">$statfile" or die "$statfile: $!\n";
    foreach my $t (@$status) {
        my @tt = @$t;
        print GRR "" . $tt[0] . " " . $tt[1] . " " . $tt[2] . "\n";
    }
    close GRR;
}


# A task is a triple (hostname q0 q1)
# This function ssh to hostname, and tests if the task is still alive,
# dead or finished.
sub check_running_task {
    my $param = shift @_;
    my $name = $param->{name};
    my $mach_desc = shift @_;
    my ($host, $q0, $q1) = @_;
    print "    $host $q0-$q1:\n";
    my @host_data=@{$mach_desc->{$host}};
    my $wdir = $host_data[1];
    my $cadodir = $host_data[2];
    # First check if the last line corresponds to finished job:
    my ($t, $lastline) = my_system_timeout(
        "ssh $host tail -1 $wdir/$name.rels.$q0-$q1", 30);
    if (! $t) {
        print "      Assume task is still running...\n";
        return 1;
    }
    $_ = $lastline;
    if (/# Total/) {
        print "      finished!\n";
        return 0;
    }
    # If file is partial check its last modification time:
    my $date;
    ($t, $date) = my_system_timeout("ssh $host date +%s", 30);
    if (! $t) {
        print "      Assume task is still running...\n";
        return 1;
    }
    my $modifdate;
    ($t, $modifdate) = my_system_timeout(
        "ssh $host stat -c %Y $wdir/$name.rels.$q0-$q1", 30);
    if (! $t) {
        print "      Assume task is still running...\n";
        return 1;
    }
    ## If didn't move for 10 minutes, assume it's dead
    if ($date > ($modifdate + 600)) {
        print "      dead ?!?\n";
        return 0;
    }
    # otherwise it's running:
    print "      running...\n";
    return 1;
}


#
# rsync a finished / dead task to the local working dir.
# Return the number of relations in the imported file.
sub import_task_result {
    my $param = shift @_;
    my $name = $param->{name};
    my $mach_desc = shift @_;
    my ($host, $q0, $q1) = @_;
    my @host_data=@{$mach_desc->{$host}};
    my $rwdir = $host_data[1];
    my $lwdir = $param->{wdir};
    my $cmd = "rsync --timeout=30 $host:$rwdir/$name.rels.$q0-$q1 $lwdir/";
    my $ret = system($cmd);
    if ($ret != 0) {
        print STDERR "Problem when importing file $name.rels.$q0-$q1 from $host.\n";
        return 0;
    }
    return `grep -v "^#" $lwdir/$name.rels.$q0-$q1 | wc -l`;
}

sub read_machine_description {
    my $param = shift @_;
    my $machine_file = $param->{machines};
    my %mach_desc;
    my $default_tmpdir='/tmp';
    my $default_cadodir='$HOME';
    open MACH, "< $machine_file";
    while (<MACH>) {
        if (/^#/) { next; }
        if (/^\s*$/) { next; }
        if (/^tmpdir=([a-zA-Z0-9_\.\/\$]*)/) {
            $default_tmpdir = $1;
            next;
        }
        if (/^cadodir=([a-zA-Z0-9_\.\/\$]*)/) {
            $default_cadodir = $1;
            next;
        }
        if (/^(\w*)\s+(\d*)\s+([a-zA-Z0-9_\.\/\$]*)\s+([a-zA-Z0-9_\.\/\$]*)/) {
            my @m = ($2, $3, $4);
            if ($m[1] eq '_') { $m[1] = $default_tmpdir; }
            if ($m[2] eq '_') { $m[2] = $default_cadodir; }
            $mach_desc{$1} = \@m;
            next;
        }
        die "Could not parse line: $_ in file $machine_file\n";
    }
    close MACH;
    return %mach_desc;
}

# Check whether .poly and .roots files are present in the remote working
# directory. Otherwise push them.
sub push_files {
    my ($mach, $wdir, $param) = @_;
    my $name = $param->{name};
    my $ldir = $param->{wdir};
    my $t;
    my $ret;
    ($t, $ret) = my_system_timeout("ssh $mach mkdir -p $wdir", 60);
    if (! $t) { die "Connection to $mach timeout\n"; }
    ($t, $ret) = my_system_timeout("ssh $mach ls $wdir/$name.poly", 120);
    if (! $t) { die "Connection to $mach timeout\n"; }
    chomp $ret;
    if ($ret eq "") {
        print "    Pushing $name.poly to $mach.\n";
        system("rsync --timeout=120 $ldir/$name.poly $mach:$wdir/");
    }
    ($t, $ret) = my_system_timeout("ssh $mach ls $wdir/$name.roots", 120);
    if (! $t) { die "Connection to $mach timeout\n"; }
    chomp $ret;
    if ($ret eq "") {
        print "    Pushing $name.roots to $mach.\n";
        system("rsync --timeout=120 $ldir/$name.roots $mach:$wdir/");
    }
}    


# Look for next special q to be launched.
# This takes into account filenames in the working directory and running
# tasks as given.
sub get_next_q {
    my ($param, $running) = @_;
    my $maxq = 0;
    my $wdir = $param->{wdir};
    my $name = $param->{name};
    opendir(DIR, $wdir) or die "can't opendir $wdir: $!";
    my @files = readdir(DIR);
    close DIR;
    foreach my $f (@files) {
        if ($f =~ /$name\.rels\.(\d+)-(\d+)/) {
            if ($2 > $maxq) {
                $maxq = $2;
            }
        }
    }
    foreach my $t (@$running) {
        if (${$t}[2] > $maxq) {
            $maxq = ${$t}[2];
        }
    }
    if ($maxq < $param->{qmin}) {
        $maxq = $param->{qmin};
    }
    return $maxq;
}

sub restart_tasks {
    my $param = shift @_;
    my $running = shift @_;
    my $mach_desc = shift @_;
    my $name = $param->{name};
    my $nice = $param->{sievenice};

    my $q_curr = get_next_q($param, $running);

    for my $m (keys %$mach_desc) {
        my $cpt = 0;
        my $t;
        my @desc = @{$mach_desc->{$m}};
        for $t (@$running) {
            if (${$t}[0] eq $m) { $cpt++; }
        }
        if ($cpt >= $desc[0]) {
            # already enough tasks running on $m
            next;
        }
        # Number of new tasks to run is:
        $cpt = $desc[0] - $cpt;
        for (my $i = 0; $i < $cpt; $i++) {
            my $qend = $q_curr + $param->{qrange};
            $t = [$m, $q_curr, $qend ];
            my $mach = $m;
            my $wdir = $desc[1];
            my $bindir = $desc[2];
            push_files($mach, $wdir, $param);
            my $cmd = "/bin/nice -$nice $bindir/sieve/las -checknorms" .
              " -I " . $param->{I} .
              " -poly $wdir/$name.poly" .
              " -fb $wdir/$name.roots" .
              " -q0 $q_curr -q1 $qend";
            my $ret = `ssh $mach "$cmd >& $wdir/$name.rels.$q_curr-$qend&"`;
            print "    Starting $mach $q_curr-$qend.\n";
            open FH, ">> " . $param->{wdir} . "/$name.cmd";
            print FH "ssh $mach \"$cmd >& $wdir/$name.rels.$q_curr-$qend&\"\n";
            close FH;
            push @$running, $t;
            $q_curr = $qend;
        }
    }
}



# returns whether some new completed file has been imported.
sub parallel_sieve_update {
    my $param = shift @_;
    my $prefix = $param->{wdir} . "/" . $param->{name};
    print "  Reading machine description file.\n";
    my %mach_desc = read_machine_description($param);
    print "  Reading status file.\n";
    my @status = read_status_file($param);
    my $new_rels=0;

    my $t;
    my @newstatus;
    # Loop over all tasks that are listed in status file and
    # check whether they are finished.
    print "  Check all running tasks:\n";
    foreach $t (@status) {
        my $running = check_running_task($param, \%mach_desc, @$t);
        if ($running) {
            push @newstatus, $t;
        } else {
            # Task is finished. 
            $new_rels += import_task_result($param, \%mach_desc, @$t);
        }
    }
    # If some tasks have finished, restart them according to mach_desc
    # and try singleton removal
    print "  Restarting tasks if necessary.\n";
    restart_tasks($param, \@newstatus, \%mach_desc);
    print "  Writing new status file.\n";
    write_status_file($param, \@newstatus);
    return $new_rels;
}

sub count_rels {
    my $param = shift @_;
    my $name = $param->{name};
    my $wdir = $param->{wdir};
    my $nrels = 0;
    opendir(DIR, $wdir) or return 0;
    my @files = readdir(DIR);
    close DIR;
    foreach my $f (@files) {
        if ($f =~ /$name\.rels\.(\d+)-(\d+)/) {
            $nrels+= `grep -v "^#" $wdir/$f | wc -l`;
        }
    }
    return $nrels;
}
 

sub parallel_sieve {
    my $param = shift @_;
    my $finished = 0;
    my $nrels = count_rels($param);
    print "We have found $nrels relations in working dir\n";
    print "Let's start the main loop!\n";
    my $prev_check = 0;
    while (! $finished) {
        print "Check what's going on on different machines...\n";
        my $new_rels = parallel_sieve_update($param);
        if ($new_rels) {
            $nrels += $new_rels;
            print "We have now $nrels relations\n";
            if ($nrels-$prev_check > $param->{checkrange}) {
                print "Trying singleton removal...\n";
                $finished = try_singleton($param, $nrels);
                $prev_check = $nrels;
            }
        }
        if (! $finished) { 
            my $delay = 120;
            print "Wait for $delay seconds before checking again.\n";
            sleep($delay);
        }
    }
    return $nrels;
}

sub sieve {
    my $param = shift @_;
    my $prefix = $param->{wdir} . "/" . $param->{name};
    my $finished=0;
    my $qcurr = $param->{qmin};
    my $nrels = 0;
    # taking (pi(2^lpba) + pi(2^lpbr)) / 3   as limit
    my $wantedrels = exp($param->{lpba}*log(2)) / ($param->{lpba}*log(2))
      + exp($param->{lpbr}*log(2)) / ($param->{lpbr}*log(2));
    $wantedrels = ceil($wantedrels/3);

    print "Sieving for $wantedrels relations\n";
    while (!$finished) {
        my $qend = $qcurr+$param->{qrange};
        my $filename = "$prefix.rels.$qcurr-$qend";
        if (! -f $filename) {
            my $cmd = $param->{cadodir} . "/sieve/las -checknorms" .
              " -I " . $param->{I} .
              " -poly $prefix.poly" .
              " -fb $prefix.roots" .
              " -q0 $qcurr -q1 $qend";
            print "Running lattice siever for q in [$qcurr,$qend]...\n";
            my_system($cmd . " >& $filename", "$prefix.cmd");
        } else {
            print "Found an existing relation file!\n";
        }
        print "Counting relations...\n";
        my $grouik = 'grep -v "^#" ' . "$filename | wc -l |";
        open WC, $grouik;
        my $lines = readline(WC);
        close WC;
        chomp $lines;
        print "  got $lines\n";
        $nrels += $lines;
        print "We have now $nrels relations; need $wantedrels\n";
        $finished = try_singleton($param, $nrels);
        $qcurr=$qend;
    }
    return $nrels;
}




MAIN: {
    my $param = read_param(@ARGV);

    # Create working directory if not there
    if (!-d $param->{wdir}) {
        my $wdir = $param->{wdir};
        mkdir $wdir or die "Cannot create $wdir: $!\n";
    }
    
    # Check if there is already some stuff relative to $name in $wdir
    # First thing is $name.n. If it is not there, we consider that
    # everything is obsolete, anyway.
    if (-f $param->{wdir} . "/" . $param->{name} . ".n") {
        print STDERR "Warning: there is already some data relative to " .
        "this name in this directory.\n";
#        die "Recovering partial computation is not implemented.\n";
    }

    # Read n if not given on command line
    if (!$param->{n}) {
        print "'n' is not given in parameters, please enter the number to factor:\n";
        $param->{n} = <STDIN>;
        chomp ($param->{n});
    }

    # Create $name.n and $name.param in wdir.
    my $prefix = $param->{wdir} . "/" . $param->{name};
    open FN, ">$prefix.n";
    print FN "n:" . $param->{n} . "\n";
    close FN;
    my $fh;
    open $fh, ">$prefix.param";
    print_param($param,$fh);

    # Polynomial selection
    polyselect($param);

    # Appending some parameters to the poly file
    open FILE, ">> $prefix.poly";
    print FILE "rlim: " . $param->{rlim}. "\n";
    print FILE "alim: " . $param->{alim}. "\n";
    print FILE "lpbr: " . $param->{lpbr}. "\n";
    print FILE "lpba: " . $param->{lpba}. "\n";
    print FILE "mfbr: " . $param->{mfbr}. "\n";
    print FILE "mfba: " . $param->{mfba}. "\n";
    print FILE "rlambda: " . $param->{rlambda}. "\n";
    print FILE "alambda: " . $param->{alambda}. "\n";

    # Creating the factor base
    print "Creating the factor base...\n";
    my $cmd = "" . $param->{cadodir} .
      "/sieve/makefb -poly $prefix.poly > $prefix.roots";
    my_system($cmd, "$prefix.cmd");

    # Computing free relations
    print "Computing free relations...\n";
    $cmd = "" . $param->{cadodir} .
      "/linalg/freerel -poly $prefix.poly -fb $prefix.roots > $prefix.freerels";
    my_system($cmd, "$prefix.cmd");

    # Sieving, removing duplicates, singleton removal
    # This is the same command, since we continue sieving until it works.
    my $nrels;
    if ($param->{parallel} eq 'true') {
        $nrels = parallel_sieve($param);
    } else {
        $nrels = sieve($param);
    }

    # Merge
    print "Merging...\n";
    $cmd = $param->{cadodir} . "/linalg/merge".
      " -mat $prefix.purged" .
      " -forbw " . $param->{bwstrat} .
      " -prune " . $param->{prune} .
      " -merge " . $param->{merge} .
      " -keep "  . $param->{keep} .
      " -maxlevel " . $param->{maxlevel} .
      " -cwmax " . $param->{cwmax} .
      " -rwmax " . $param->{rwmax} .
      " -ratio " . $param->{ratio};
    my_system($cmd . "> $prefix.merge.his 2> $prefix.merge.stderr",
        "$prefix.cmd");
    my $bwcostmin=`tail $prefix.merge.his | grep "BWCOSTMIN:" | awk '{print \$NF}'`;
    chomp $bwcostmin;
    print "Bwcostmin = $bwcostmin\n";
    $cmd = $param->{cadodir} . "/linalg/replay" .
      " $prefix.purged $prefix.merge.his" .
      " $prefix.small $prefix.index $bwcostmin";
    my_system($cmd . " 2> $prefix.replay.stderr", "$prefix.cmd");

    # Linear algebra
    print "Transposing...\n";
    $cmd = $param->{cadodir} . "/linalg/transpose" .
      "  -in $prefix.small -out $prefix.small.tr";
    my_system($cmd, "$prefix.cmd");
    if ($param->{linalg} eq 'bw') { 
        print "Calling Block-Wiedemann...\n";
        $cmd = $param->{cadodir} . "/linalg/bw/bw.pl" .
        " mt=" . $param->{bwmt} .
        " matrix=$prefix.small.tr" .
        " mn=64" .
        " vectoring=64" .
        " multisols=1" .
        " wdir=" . $param->{wdir} . "/bw" .
        " solution=$prefix.W";
        my_system($cmd . " >& $prefix.bw.stderr", "$prefix.cmd");
    } else {
        if ($param->{linalg} ne 'bl') {
            print "WARNING: I don't know linalg=" . $param->{linalg} .
              ". Use bl as default.\n";
        }
        print "Calling Block-Lanczos...\n";
        $cmd = $param->{cadodir} . "/linalg/bl/bl.pl" .
        " matrix=$prefix.small.tr" .
        " wdir=" . $param->{wdir} . "/bl" .
        " solution=$prefix.W";
        my_system($cmd . " >& $prefix.bl.stderr", "$prefix.cmd");
    }
    print "Converting dependencies to CADO format...\n";
    $cmd = $param->{cadodir} . "/linalg/bw/mkbitstrings " .
      " $prefix.W";
    my_system($cmd . " > $prefix.ker_raw", "$prefix.cmd");
    my $nker = `wc -l < $prefix.ker_raw`;
    chomp $nker;
    print "We have computed $nker vectors of the Kernel.\n";

    # Characters
    print "Adding characters...\n";
    $cmd = $param->{cadodir} . "/linalg/characters" .
      " -poly $prefix.poly" .
      " -purged $prefix.purged" .
      " -ker $prefix.ker_raw" .
      " -index $prefix.index" .
      " -rel $prefix.nodup" .
      " -small $prefix.small" .
      " -nker $nker" .
      " -nchar " . $param->{nchar};
    my_system($cmd . " > $prefix.ker 2> $prefix.characters.stderr",
        "$prefix.cmd");

    my $ndepmax=`wc -l $prefix.ker | awk '{print \$1}'`;
    chomp $ndepmax;
    print "We have $ndepmax remaining after characters.\n";
    if ($ndepmax > $param->{nkermax}) {
        $ndepmax = $param->{nkermax};
    }

    # Sqrt
    print "Preparing $ndepmax squareroots...\n";
    $cmd = $param->{cadodir} . "/linalg/allsqrt" .
      " $prefix.nodup $prefix.purged $prefix.index $prefix.ker" .
      " $prefix.poly" .
      " 0 $ndepmax ar $prefix.dep";
    my_system($cmd, "$prefix.cmd");
    
    my $i;
    for ($i = 0; $i < $ndepmax; $i++) {
        my $suf = sprintf("%03d", $i);
        print "Testing dependency numnber $i...\n";
        $cmd = $param->{cadodir} . "/sqrt/naive/algsqrt" .
          "  $prefix.dep.alg.$suf $prefix.dep.rat.$suf $prefix.poly";
        my_system($cmd . "> $prefix.fact.$suf 2>> $prefix.algsqrt.stderr",
            "$prefix.cmd");
        open FH, "< $prefix.fact.$suf";
        my $line = <FH>; chomp $line;
        if ($line ne 'Failed') {
            print $line . " ";
            $line = <FH>;
            print $line;
            close FH;
            last;
        }
        close FH;
    }
}
