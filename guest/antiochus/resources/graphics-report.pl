#!/usr/bin/perl
# SPDX-License-Identifier: GPL-2.0-or-later
# Works in Tiger's Perl. The same report consumes Claude's per-case host evidence.
use strict;
use warnings;
use POSIX qw(:sys_wait_h);
use File::Spec;
use File::Basename qw(basename);
use Time::HiRes qw(time sleep);

sub esc { my $s=defined $_[0]?$_[0]:''; $s=~s/&/&amp;/g; $s=~s/</&lt;/g; $s=~s/>/&gt;/g; $s=~s/"/&quot;/g; return $s; }
sub readfile { my ($f)=@_; open(my $h,'<',$f) or return ''; binmode $h; local $/; return <$h> || ''; }
sub store { my ($path,$bytes)=@_; open(my $h,'>',$path) or die "$path: $!";binmode $h;print {$h} $bytes or die "$path: $!";close $h or die "$path: $!"; }
sub records { return map { [split(/\t/,$_,2)] } grep { /\t/ } split(/\n/,$_[0]); }
sub chunk { my ($tag,$data)=@_; return pack('N',length $data).$tag.$data.pack('N',Compress::Zlib::crc32($tag.$data)); }
sub preview {
    my ($file)=@_; return unless eval { require Compress::Zlib; 1 };
    my $ppm=readfile($file);
    return unless $ppm=~s/\AP6\n(\d+) (\d+)\n255\n//;
    my ($w,$h)=($1,$2); return unless $w>0 && $h>0 && length($ppm)==$w*$h*3;
    my $img=join('', map {"\0".substr($ppm,$_*$w*3,$w*3)} 0..$h-1);
    my $out=$file; $out=~s/\.ppm$/.png/;
    store($out,"\x89PNG\r\n\x1a\n".chunk('IHDR',pack('NNCCCCC',$w,$h,8,2,0,0,0)).
        chunk('IDAT',Compress::Zlib::compress($img)).chunk('IEND',''));
    return basename($out);
}
my ($mode,$root,$worker)=@ARGV;
die "usage: graphics-report.pl --run DIRECTORY WORKER | --report DIRECTORY\n"
    unless defined $root && ($mode eq '--run' || $mode eq '--report');
$root=File::Spec->rel2abs($root);
my ($child,$cancel)=(0,0);
if($mode eq '--run') {
    die "Missing worker" unless defined $worker && -x $worker;
    die "Run directory already used" if -e "$root/manifest.tsv";
    open(my $manifest,'-|',$worker,'--list-graphics') or die "Cannot list tests";
    my @lines=<$manifest>; close $manifest or die "Worker manifest failed";
    store("$root/manifest.tsv",join('',@lines));
    $SIG{TERM}=$SIG{INT}=sub { $cancel=1; kill 'TERM',$child if $child; };
}
my @cases;
my %seen;
for my $line (split /\n/,readfile("$root/manifest.tsv")) {
    my ($n,$id,$suite,$version,$path)=split(/\t/,$line,5);
    die "Bad manifest row: $line" unless defined $path && $n=~/^\d+$/ && $id=~/^(?:cf|sr)-[a-z0-9-]+$/ && !$seen{$id}++;
    push @cases,[$n,$id,$suite,$version,$path];
}
die "Empty manifest" unless @cases;
if($mode eq '--run') {
    for my $c (@cases) {
        last if $cancel;
        my ($ord,$id)=@$c;my $d="$root/$id";mkdir($d,0700) or die "$d: $!";
        store("$root/current.tsv","$ord\t$id\n");
        my $start=time;
        $child=fork(); die "fork: $!" unless defined $child;
        if(!$child) {
            $SIG{TERM}=$SIG{INT}='DEFAULT';
            open(STDOUT,'>',"$d/result.tsv") or exit 125;
            open(STDERR,'>',"$d/stderr.log") or exit 125;
            exec {$worker} $worker,$id,$d;exit 126;
        }
        my ($killed,$timed)=(0,0);
        my $exit;
        while(1) {
            my $state=waitpid($child,WNOHANG);
            if($state==$child) { $exit=$?;last; }
            if($state<0) { next if $!{EINTR};die "waitpid: $!"; }
            if($cancel || time-$start>=150) {
                if(!$killed) { $timed=!$cancel; kill 'TERM',$child; $killed=time; }
                elsif(time-$killed>2) { kill 'KILL',$child; }
            }
            sleep .08;
        }
        $child=0;
        store("$d/runner.tsv",sprintf("clock_domain\tguest_wall\nstart_epoch\t%.6f\nend_epoch\t%.6f\nwait_status\t%d\n",$start,time,$exit));
        my $status=$cancel?'incomplete':$timed?'timeout':($exit&127)?'crash':'';
        if($status) {
            my $cause=$cancel?'Owner stopped suite':$timed?'150 second case deadline':'signal '.($exit&127);
            store("$d/host.tsv","status\t$status\nreason\t$cause\n");
        }
    }
}
my ($rows,$details)=('','');my %tot;my $issues=0;
for my $c (@cases) {
    my ($ord,$id,$suite,$version,$source)=@$c;my $d="$root/$id";
    my $raw=readfile("$d/result.tsv");my @events=records($raw);
    my %host=map { @$_ } records(readfile("$d/host.tsv"));
    my (%v,%side_status,@observations,@reason);
    my ($side,$line,$cmd,$step,$ticks,$warnings,$last_gl)=('','','','','',0,'');
    my @markers;
    for my $row (@events) {
        my ($k,$a)=@$row; $v{$k}=$a;
        push @markers,$a if $k eq 'cf_marker';
        if($k eq 'side') { $side=$a; $line=$cmd=$last_gl=$step=$ticks=''; }
        $line=$a if $k eq 'source_line';$cmd=$a if $k eq 'command';$step=$a if $k eq 'step';$ticks=$a if $k eq 'command_ticks';
        $warnings++ if $k eq 'upstream_warning';
        $side_status{$side}=$a if $k eq 'side_status';
        if($k eq 'gl_error' && $a ne '0') { $last_gl=$a;push @observations,"$side | $cmd | $step | GL error $a"; }
        if($k eq 'reason') {push @reason,"$side $a";push @observations,"$side | line $line $cmd | $step | $a";}
        if($k=~/^(?:gl_vendor|gl_renderer|renderer_id|machine|kernel|gl_version|glsl_version|viewport|scissor|draw_buffer|read_buffer|active_texture|texture_2d|compile_log|link_log|missing_extension|driver_log|arb_error_position|arb_driver_log|arb_under_native_limits|arb_instructions|arb_native_instruction_limit|side_status|first_x|first_y|actual_channel|expected_channel|max_channel_error_normalized|probe_status|upstream_warning|actual_rgba|expected_rgba|probe_plane)$/) {
            push @observations,"$side | line $line $cmd | $k=$a" if length($a) && !($k eq 'max_channel_error_normalized' && $a==0);
        }
        if($k eq 'bad_pixels' && $a>0){push @observations,"$side | line $line $cmd | ticks $ticks | bad_pixels=$a";}
    }
    my $status=(($host{status}||'') eq 'pass' ? '' : $host{status}) || $v{status} || 'incomplete';
    $status='incomplete' unless $status=~/^(pass|unsupported|fail|timeout|crash|incomplete)$/;
    my $beg=@markers && $markers[0]=~/^begin \Q$ord\E \Q$id\E (\d+)$/ ? $1 : undef;
    my $ended=@markers==2 && $markers[1]=~/^end \Q$ord\E \Q$id\E (\d+)$/ ? $1 : undef;
    my $complete=defined $beg && defined $ended && $ended>=$beg && ($v{id}||'') eq $id;
    if((!$host{status} || $status eq 'pass') && !$complete) {$status='incomplete';}
    $status='warn' if $status eq 'pass' && $warnings;
    my %opt=map { @$_ } records(readfile("$d/optics.tsv"));
    my @signals=grep { /^(?:refused_delta|approx_\w+_delta|device_failed|shader_errors)$/ && $opt{$_}=~/^\d+$/ && $opt{$_}>0 } sort keys %opt;
    my $signal=(@signals && ($opt{scope}||'') eq 'case')?join(', ',map {"$_=$opt{$_}"} @signals):'';
    $tot{$status}++;my $issue=$status ne 'pass' || $signal ne ''; $issues++ if $issue;
    push @reason, "Host: $signal" if $signal;
    push @reason,$host{reason} if defined $host{reason};
    my $optics=-s "$d/optics.log";my $regs=-s "$d/registers.log";
    my $result=$issue?"<a href=\"#$id\">$status</a>":$status;
    $rows.="<tr><td>$ord<td>".esc($source)."<br><small>".(-f "$d/result.tsv" ? "<a href=\"$id/result.tsv\">$id</a>" : $id)."</small><td>$result<td>".esc(join(' / ',@reason))."</tr>\n";
    next unless $issue;
    my @links;my @pics;
    if(!-d $d){$details.="<h3 id=\"$id\">$id</h3><p>Case directory missing.</p>";next;}
    opendir(my $dh,$d) or die "$d: $!";
    for my $file (sort readdir $dh) {
        next unless $file=~/^[a-zA-Z0-9_.-]+\.(?:tsv|txt|log|ppm|rgba|frag|vert|vp|fp|f32)$/ && -f "$d/$file";
        push @links,"<a href=\"$id/$file\">".esc($file)."</a>";
        if($file=~/\.ppm$/) {
            my $png=preview("$d/$file");push @pics,"<span><img src=\"$id/$png\" alt=\"$file\"><br>".esc($file)."</span>" if $png;
        }
    }
    closedir $dh;
    my $origin=$optics?'Host Optics log attached':'Host Optics evidence unavailable';
    $origin.=$regs?'; register trace attached':'; register trace unavailable';
    my $signals=join(', ',map {"$_=$side_status{$_}"} sort keys %side_status);
    $details.="<h3 id=\"$id\">$ord · ".esc($source)."</h3><p>".esc("$status $signals · $origin")."</p>".
        "<p>Last recorded command / step: ".esc("$side $line $cmd / $step")."</p><p>".join(' · ',@links)."</p><div>".
        join(' ',@pics)."</div><pre>".esc(join("\n",@observations))."</pre>\n";
}
my $env=readfile("$root/environment.tsv");
my $environment=$env eq '' ? '' : '<h2>Run environment</h2><pre>'.esc($env).'</pre>';
my $summary=join(', ',map {"$_ $tot{$_}"} sort keys %tot);
store("$root/summary.tsv",join('',map {"$_\t$tot{$_}\n"} sort keys %tot));
store("$root/index.html","<!DOCTYPE html><meta charset=\"utf-8\"><title>Antiochus · Piglit fork</title>".
    "<style>body{font:15px Helvetica,sans-serif;margin:24px}td{padding:4px 12px 4px 0}img{max-width:250px}span{display:inline-block;margin-right:12px}pre{white-space:pre-wrap}h3{border-top:1px solid #999;padding-top:15px}</style>".
    "<h1>Antiochus · Piglit fork</h1><p>".scalar(@cases)." cases · ".esc($summary)."</p>$environment".
    "<p>Probe coordinates start at the lower left. Pixel diffs cover the upstream assertion. Driver and Optics attribution needs host evidence.</p>".
    "<table><tr><th>#<th>Source test<th>Verdict<th>Reason</tr>$rows</table>$details");
print "$summary\n";
exit($cancel?2:0);
