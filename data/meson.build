configure_file(
  input: 'power-profiles-daemon.service.in',
  output: 'power-profiles-daemon.service',
  configuration: {
    'libexecdir': libexecdir,
  },
  install_dir: systemd_system_unit_dir,
)

foreach name, _: bus_names
  config = {
    'dbus_name': name,
    'dbus_iface': name,
  }

  configure_file(
    input: 'power-profiles-daemon.dbus.conf.in',
    output: name + '.conf',
    configuration: config,
    install_dir: dbusconfdir
  )

  configure_file(
    input: 'power-profiles-daemon.dbus.service.in',
    output: name + '.service',
    configuration: config,
    install_dir: dbusservicedir
  )
endforeach

polkit_policy = 'power-profiles-daemon.policy'
if xmllint.found()
  test(polkit_policy,
       xmllint,
       args: [
           '--noout',
           meson.project_source_root() / 'data' / polkit_policy,
       ])
endif

install_data(
  polkit_policy,
  install_dir: polkit_policy_directory,
)
