package ProFTPD::Tests::Modules::mod_pool;

use lib qw(t/lib);
use base qw(ProFTPD::TestSuite::Child);
use strict;

use Cwd;
use File::Path qw(mkpath);
use File::Spec;
use IO::Handle;

use ProFTPD::TestSuite::FTP;
use ProFTPD::TestSuite::Utils qw(:auth :config :running :test :testsuite);

$| = 1;

my $order = 0;

my $TESTS = {
  pool_ftp_downloads_binary => {
    order => ++$order,
    test_class => [qw(forking)],
  },

  pool_ftp_downloads_ascii => {
    order => ++$order,
    test_class => [qw(forking)],
  },

  pool_ftp_download_ascii_largefile => {
    order => ++$order,
    test_class => [qw(forking)],
  },

# XXX Need single LARGE (>2GB) binary
# XXX Need FTPS versions

  pool_ftp_uploads_binary => {
    order => ++$order,
    test_class => [qw(forking)],
  },

  pool_ftp_uploads_ascii => {
    order => ++$order,
    test_class => [qw(forking)],
  },

  pool_ftp_upload_ascii_largefile => {
    order => ++$order,
    test_class => [qw(forking)],
  },

# XXX Need single LARGE (>2GB) binary
# XXX Need FTPS versions

  pool_ftp_dirlists_list => {
    order => ++$order,
    test_class => [qw(forking)],
  },

  pool_ftp_dirlists_nlst => {
    order => ++$order,
    test_class => [qw(forking)],
  },

  pool_ftp_dirlists_mlst => {
    order => ++$order,
    test_class => [qw(forking)],
  },

  pool_ftp_dirlists_mlsd => {
    order => ++$order,
    test_class => [qw(forking)],
  },

  pool_sftp_downloads => {
    order => ++$order,
    test_class => [qw(forking mod_sftp)],
  },

  pool_sftp_uploads => {
    order => ++$order,
    test_class => [qw(forking mod_sftp)],
  },

};

sub new {
  return shift()->SUPER::new(@_);
}

sub list_tests {
#  return testsuite_get_runnable_tests($TESTS);
  return qw(
    pool_ftp_upload_ascii_largefile
  );
}

sub pool_ftp_downloads_binary {
  my $self = shift;
  my $tmpdir = $self->{tmpdir};
  my $setup = test_setup($tmpdir, 'pool');

  my $pool_logs = File::Spec->rel2abs("$tmpdir/pool.d");
  mkpath($pool_logs);

  my $test_file = File::Spec->rel2abs($setup->{config_file});

  my $config = {
    PidFile => $setup->{pid_file},
    ScoreboardFile => $setup->{scoreboard_file},
    SystemLog => $setup->{log_file},

    AuthUserFile => $setup->{auth_user_file},
    AuthGroupFile => $setup->{auth_group_file},

    IfModules => {
      'mod_delay.c' => {
        DelayEngine => 'off',
      },

      'mod_pool.c' => {
        PoolEngine => 'on',
        PoolLogs => $pool_logs,
        PoolEvents => 'Downloads',
      },
    },
  };

  my ($port, $config_user, $config_group) = config_write($setup->{config_file},
    $config);

  # Open pipes, for use between the parent and child processes.  Specifically,
  # the child will indicate when it's done with its test by writing a message
  # to the parent.
  my ($rfh, $wfh);
  unless (pipe($rfh, $wfh)) {
    die("Can't open pipe: $!");
  }

  my $ex;

  # Fork child
  $self->handle_sigchld();
  defined(my $pid = fork()) or die("Can't fork: $!");
  if ($pid) {
    eval {
      my $client = ProFTPD::TestSuite::FTP->new('127.0.0.1', $port, 0, 1);
      $client->login($setup->{user}, $setup->{passwd});
      $client->type('binary');

      my $count = 10;
      for (my $i = 0; $i < $count; $i++) {
        my $conn = $client->retr_raw($test_file);
        unless ($conn) {
          die("Failed to RETR: " . $client->response_code() . " " .
            $client->response_msg());
        }

        my $buf;
        while ($conn->read($buf, 8192, 30)) {
        }
        eval { $conn->close() };

        my ($resp_code, $resp_msg);
        $resp_code = $client->response_code();
        $resp_msg = $client->response_msg();
        $self->assert_transfer_ok($resp_code, $resp_msg);
      }

      $client->quit();
    };

    if ($@) {
      $ex = $@;
    }

    $wfh->print("done\n");
    $wfh->flush();

  } else {
    eval { server_wait($setup->{config_file}, $rfh) };
    if ($@) {
      warn($@);
      exit 1;
    }

    exit 0;
  }

  # Stop server
  server_stop($setup->{pid_file});
  $self->assert_child_ok($pid);

  test_cleanup($setup->{log_file}, $ex);
}

sub pool_ftp_downloads_ascii {
  my $self = shift;
  my $tmpdir = $self->{tmpdir};
  my $setup = test_setup($tmpdir, 'pool');

  my $pool_logs = File::Spec->rel2abs("$tmpdir/pool.d");
  mkpath($pool_logs);

  my $test_file = File::Spec->rel2abs($setup->{config_file});

  my $config = {
    PidFile => $setup->{pid_file},
    ScoreboardFile => $setup->{scoreboard_file},
    SystemLog => $setup->{log_file},

    AuthUserFile => $setup->{auth_user_file},
    AuthGroupFile => $setup->{auth_group_file},

    IfModules => {
      'mod_delay.c' => {
        DelayEngine => 'off',
      },

      'mod_pool.c' => {
        PoolEngine => 'on',
        PoolLogs => $pool_logs,
        PoolEvents => 'Downloads',
      },
    },
  };

  my ($port, $config_user, $config_group) = config_write($setup->{config_file},
    $config);

  # Open pipes, for use between the parent and child processes.  Specifically,
  # the child will indicate when it's done with its test by writing a message
  # to the parent.
  my ($rfh, $wfh);
  unless (pipe($rfh, $wfh)) {
    die("Can't open pipe: $!");
  }

  my $ex;

  # Fork child
  $self->handle_sigchld();
  defined(my $pid = fork()) or die("Can't fork: $!");
  if ($pid) {
    eval {
      my $client = ProFTPD::TestSuite::FTP->new('127.0.0.1', $port, 0, 1);
      $client->login($setup->{user}, $setup->{passwd});
      $client->type('ascii');

      my $count = 10;
      for (my $i = 0; $i < $count; $i++) {
        my $conn = $client->retr_raw($test_file);
        unless ($conn) {
          die("Failed to RETR: " . $client->response_code() . " " .
            $client->response_msg());
        }

        my $buf;
        while ($conn->read($buf, 8192, 30)) {
        }
        eval { $conn->close() };

        my ($resp_code, $resp_msg);
        $resp_code = $client->response_code();
        $resp_msg = $client->response_msg();
        $self->assert_transfer_ok($resp_code, $resp_msg);
      }

      $client->quit();
    };

    if ($@) {
      $ex = $@;
    }

    $wfh->print("done\n");
    $wfh->flush();

  } else {
    eval { server_wait($setup->{config_file}, $rfh) };
    if ($@) {
      warn($@);
      exit 1;
    }

    exit 0;
  }

  # Stop server
  server_stop($setup->{pid_file});
  $self->assert_child_ok($pid);

  test_cleanup($setup->{log_file}, $ex);
}

# See Bug#4277.
sub pool_ftp_download_ascii_largefile {
  my $self = shift;
  my $tmpdir = $self->{tmpdir};
  my $setup = test_setup($tmpdir, 'pool');

  my $pool_logs = File::Spec->rel2abs("$tmpdir/pool.d");
  mkpath($pool_logs);

  my $test_file = File::Spec->rel2abs("$tmpdir/test.txt");
  if (open(my $fh, "> $test_file")) {
    my $buf = "a\nb\nc\nd\ne\nf\ng\n" x 8192;
    my $count = 100;
    for (my $i = 0; $i < $count; $i++) {
      print $fh $buf;
    }

    unless (close($fh)) {
      die("Can't write $test_file: $!");
    }

  } else {
    die("Can't open $test_file: $!");
  }

  my $config = {
    PidFile => $setup->{pid_file},
    ScoreboardFile => $setup->{scoreboard_file},
    SystemLog => $setup->{log_file},

    AuthUserFile => $setup->{auth_user_file},
    AuthGroupFile => $setup->{auth_group_file},

    IfModules => {
      'mod_delay.c' => {
        DelayEngine => 'off',
      },

      'mod_pool.c' => {
        PoolEngine => 'on',
        PoolLogs => $pool_logs,
        PoolEvents => 'Downloads',
      },
    },
  };

  my ($port, $config_user, $config_group) = config_write($setup->{config_file},
    $config);

  # Open pipes, for use between the parent and child processes.  Specifically,
  # the child will indicate when it's done with its test by writing a message
  # to the parent.
  my ($rfh, $wfh);
  unless (pipe($rfh, $wfh)) {
    die("Can't open pipe: $!");
  }

  my $ex;

  # Fork child
  $self->handle_sigchld();
  defined(my $pid = fork()) or die("Can't fork: $!");
  if ($pid) {
    eval {
      my $client = ProFTPD::TestSuite::FTP->new('127.0.0.1', $port, 0, 1);
      $client->login($setup->{user}, $setup->{passwd});
      $client->type('ascii');

      my $conn = $client->retr_raw($test_file);
      unless ($conn) {
        die("Failed to RETR: " . $client->response_code() . " " .
          $client->response_msg());
      }

      my $buf;
      while ($conn->read($buf, 8192, 30)) {
      }
      eval { $conn->close() };

      my $resp_code = $client->response_code();
      my $resp_msg = $client->response_msg();
      $self->assert_transfer_ok($resp_code, $resp_msg);

      $client->quit();
    };

    if ($@) {
      $ex = $@;
    }

    $wfh->print("done\n");
    $wfh->flush();

  } else {
    eval { server_wait($setup->{config_file}, $rfh) };
    if ($@) {
      warn($@);
      exit 1;
    }

    exit 0;
  }

  # Stop server
  server_stop($setup->{pid_file});
  $self->assert_child_ok($pid);

  test_cleanup($setup->{log_file}, $ex);
}

# See Bug#4277.
sub pool_ftp_upload_ascii_largefile {
  my $self = shift;
  my $tmpdir = $self->{tmpdir};
  my $setup = test_setup($tmpdir, 'pool');

  my $pool_logs = File::Spec->rel2abs("$tmpdir/pool.d");
  mkpath($pool_logs);

  my $test_file = File::Spec->rel2abs("$tmpdir/test.txt");

  my $config = {
    PidFile => $setup->{pid_file},
    ScoreboardFile => $setup->{scoreboard_file},
    SystemLog => $setup->{log_file},

    AuthUserFile => $setup->{auth_user_file},
    AuthGroupFile => $setup->{auth_group_file},

    IfModules => {
      'mod_delay.c' => {
        DelayEngine => 'off',
      },

      'mod_pool.c' => {
        PoolEngine => 'on',
        PoolLogs => $pool_logs,
        PoolEvents => 'Uploads',
      },
    },
  };

  my ($port, $config_user, $config_group) = config_write($setup->{config_file},
    $config);

  # Open pipes, for use between the parent and child processes.  Specifically,
  # the child will indicate when it's done with its test by writing a message
  # to the parent.
  my ($rfh, $wfh);
  unless (pipe($rfh, $wfh)) {
    die("Can't open pipe: $!");
  }

  my $ex;

  # Fork child
  $self->handle_sigchld();
  defined(my $pid = fork()) or die("Can't fork: $!");
  if ($pid) {
    eval {
      my $client = ProFTPD::TestSuite::FTP->new('127.0.0.1', $port, 0, 1);
      $client->login($setup->{user}, $setup->{passwd});
      $client->type('ascii');

      my $conn = $client->stor_raw($test_file);
      unless ($conn) {
        die("Failed to STOR: " . $client->response_code() . " " .
          $client->response_msg());
      }

      my $buf = "a\nb\nc\nd\ne\nf\ng\n" x 8192;
      my $count = 100;
      for (my $i = 0; $i < $count; $i++) {
        $conn->write($buf, length($buf), 10);
      }
      eval { $conn->close() };

      my $resp_code = $client->response_code();
      my $resp_msg = $client->response_msg();
      $self->assert_transfer_ok($resp_code, $resp_msg);

      $client->quit();
    };

    if ($@) {
      $ex = $@;
    }

    $wfh->print("done\n");
    $wfh->flush();

  } else {
    eval { server_wait($setup->{config_file}, $rfh) };
    if ($@) {
      warn($@);
      exit 1;
    }

    exit 0;
  }

  # Stop server
  server_stop($setup->{pid_file});
  $self->assert_child_ok($pid);

  test_cleanup($setup->{log_file}, $ex);
}

1;
