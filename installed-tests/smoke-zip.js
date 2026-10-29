// SPDX-License-Identifier: GPL-2.0-or-later

import Gio from 'gi://Gio';
import GLib from 'gi://GLib';

// Run with: gjs -m installed-tests/smoke-zip.js _build/zenlink@zenos-n.github.io
if (ARGV.length !== 1)
    throw new Error('Expected the unpacked extension directory');

const directory = Gio.File.new_for_path(ARGV[0]);
const {default: Config} = await import(directory.get_child('config.js').get_uri());
const Setup = await import(directory.get_child('utils/setup.js').get_uri());

Config.GSETTINGS_SCHEMA_DIR = directory.get_child('schemas').get_path();
Setup.setup(directory.get_path());

const schema = Config.GSCHEMA.lookup(Config.APP_ID, false);
if (!schema || !schema.has_key('devices'))
    throw new Error('The packaged device settings schema could not be loaded');

const resource = Gio.resources_lookup_data(
    `${Config.APP_PATH}/ui/preferences-window.ui`, Gio.ResourceLookupFlags.NONE);
if (resource.get_size() === 0)
    throw new Error('The packaged preferences resource is empty');

const metadataPath = directory.get_child('metadata.json').get_path();
const [, bytes] = GLib.file_get_contents(metadataPath);
const metadata = JSON.parse(new TextDecoder().decode(bytes));
if (metadata.uuid !== 'zenlink@zenos-n.github.io' || metadata.name !== 'ZenLink')
    throw new Error('The package does not identify itself as ZenLink');

print('ZenLink package settings, resources, bootstrap and metadata passed');
