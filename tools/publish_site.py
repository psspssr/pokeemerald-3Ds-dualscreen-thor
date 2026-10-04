#!/usr/bin/env python3
"""Publish only the generated website to its dedicated public Pages repository.

WEBSITE_DEPLOY_KEY is a deploy key scoped to that repository. Game source,
APKs, data packs and build diagnostics are never copied to the public checkout.
"""
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
from urllib.request import urlopen

from build_site import source_file, validate_artifact

ROOT = Path(__file__).resolve().parents[1]
TARGET = 'git@github.com:psspssr/emerald-dual-screen-site.git'
SOURCE = 'psspssr/pokeemerald-3Ds-dualscreen-thor'


def publish():
    output = ROOT / 'build/site'
    validate_artifact(output)
    release = json.loads((output / 'release.json').read_text())
    if release['repository'] != SOURCE:
        raise ValueError('unexpected game release repository')
    license_file = source_file(ROOT, 'LICENSE')
    private_key = os.environ.get('WEBSITE_DEPLOY_KEY')
    if not private_key:
        raise ValueError('WEBSITE_DEPLOY_KEY is required')
    with tempfile.TemporaryDirectory(prefix='emerald-website-') as directory:
        temporary = Path(directory)
        key = temporary / 'deploy-key'
        key.write_text(private_key.rstrip() + '\n')
        key.chmod(0o600)
        # GitHub publishes its host keys over authenticated HTTPS. Do not
        # trust an unauthenticated ssh-keyscan or disable host verification.
        with urlopen('https://api.github.com/meta', timeout=30) as response:
            host_keys = json.load(response)['ssh_keys']
        if not host_keys or any('\n' in value or not value.startswith(('ssh-', 'ecdsa-')) for value in host_keys):
            raise ValueError('unexpected GitHub host-key metadata')
        known_hosts = temporary / 'known_hosts'
        known_hosts.write_text(''.join('github.com ' + value + '\n' for value in host_keys))
        env = os.environ.copy()
        for name in ('WEBSITE_DEPLOY_KEY', 'GH_TOKEN', 'GITHUB_TOKEN'):
            env.pop(name, None)
        env['GIT_TERMINAL_PROMPT'] = '0'
        env['GIT_SSH_COMMAND'] = shlex.join(['ssh', '-i', str(key), '-o', 'IdentitiesOnly=yes',
            '-o', 'BatchMode=yes', '-o', 'StrictHostKeyChecking=yes', '-o', f'UserKnownHostsFile={known_hosts}'])

        def git(*args, cwd=None, check=True):
            return subprocess.run(['git', *args], cwd=cwd, env=env, check=check,
                                  text=True, capture_output=True, timeout=120)

        checkout = temporary / 'public-site'
        existing = git('ls-remote', '--exit-code', '--heads', TARGET, 'main', check=False)
        if existing.returncode == 0:
            git('clone', '--depth=1', '--branch=main', TARGET, str(checkout))
        elif existing.returncode == 2:
            checkout.mkdir()
            git('init', '-b', 'main', cwd=checkout)
            git('remote', 'add', 'origin', TARGET, cwd=checkout)
        else:
            raise RuntimeError('cannot read the website repository: ' + existing.stderr.strip())
        # This repository contains generated website files only. Replace its
        # published tree while preserving normal, fast-forward Git history.
        for item in checkout.iterdir():
            if item.name == '.git':
                continue
            if item.is_dir() and not item.is_symlink():
                shutil.rmtree(item)
            else:
                item.unlink()
        shutil.copytree(output, checkout, dirs_exist_ok=True)
        shutil.copyfile(license_file, checkout / 'LICENSE')
        (checkout / 'README.md').write_text(
            '# Emerald Dual Screen website\n\n'
            'Live: https://psspssr.github.io/emerald-dual-screen-site/\n\n'
            'This repository contains only the generated public website. '
            'The game repository and its releases retain their own access controls.\n\n'
            'Website source and publishing instructions are maintained in '
            f'[{SOURCE}](https://github.com/{SOURCE}/tree/dev/site). '
            'Edit that source; the publishing workflow replaces generated files here.\n\n'
            'Original website code uses the MIT licence. Fonts retain SIL OFL 1.1; '
            'game images, trademarks and device imagery retain their respective owners’ rights. '
            'See credits.html for credits and the as-is/no-support notice.\n')
        git('config', 'user.name', 'psspssr', cwd=checkout)
        git('config', 'user.email', '125391196+psspssr@users.noreply.github.com', cwd=checkout)
        git('add', '--all', cwd=checkout)
        changed = git('diff', '--cached', '--quiet', check=False, cwd=checkout)
        if changed.returncode == 0:
            print('Website already matches the latest complete release.')
            return
        if changed.returncode != 1:
            raise RuntimeError('cannot inspect staged website changes')
        git('commit', '-m', f'Publish website for {release["tag"]}', cwd=checkout)
        git('push', 'origin', 'HEAD:refs/heads/main', cwd=checkout)
        print('Published website files for', release['tag'])


if __name__ == '__main__':
    publish()
