data_conf = configuration_data()
data_conf.set('libexecdir', libexecdir)

configure_file(
  input: 'power-profiles-daemon.service.in',
  output: 'power-profiles-daemon.service',
  configuration: data_conf,
  install_dir: systemd_system_unit_dir,
)

configure_file(
  input: 'net.hadess.PowerProfiles.conf.in',
  output: 'net.hadess.PowerProfiles.conf',
  configuration: data_conf,
  install_dir: dbusconfdir
)
