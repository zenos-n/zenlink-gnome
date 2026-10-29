# SPDX-FileCopyrightText: GSConnect Developers https://github.com/GSConnect
#
# SPDX-License-Identifier: GPL-2.0-or-later

# Nautilus Extension
if get_option('nautilus')
  install_data(
    'nautilus-zenlink.py',
    install_dir: join_paths(datadir, 'nautilus-python', 'extensions')
  )
endif

# Nemo Extension
if get_option('nemo')
  install_data(
    'nautilus-zenlink.py',
    rename: join_paths(datadir, 'nemo-python', 'extensions', 'nemo-zenlink.py')
  )
endif

