#!/usr/bin/env python3
"""
Uefinity Dependency Management System
=====================================

This script manages third-party dependencies for the Uefinity bootloader project.
It reads a YAML configuration file and ensures all dependencies are properly
checked out to the correct versions.

Features:
- Supports git branches, tags, and specific commit hashes
- Creates stamp files to track checkout status
- Force re-checkout capability
- Robust error handling and logging
- Detailed status reporting

Author: Ahmed ARIF (Arif193@gmail.com)
License: MIT
"""

import argparse
import hashlib
import logging
import os
import subprocess
import sys
import yaml
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional, Union


class DependencyManager:
    """Main class for managing third-party dependencies."""

    def __init__(self, config_path: str, base_dir: str, force: bool = False):
        """
        Initialize the dependency manager.

        Args:
            config_path: Path to the YAML configuration file
            base_dir: Base directory for the project (usually where deps.yaml is)
            force: Force re-checkout of all dependencies
        """
        self.config_path = Path(config_path)
        self.base_dir = Path(base_dir)
        self.force = force
        self.thirdparty_dir = self.base_dir / "3rdparty"
        self.stamp_dir = self.thirdparty_dir / ".stamps"

        # Set up logging
        self._setup_logging()

        # Ensure directories exist
        self.thirdparty_dir.mkdir(exist_ok=True)
        self.stamp_dir.mkdir(exist_ok=True)

        # Load configuration
        self.config = self._load_config()

    def _setup_logging(self):
        """Set up logging configuration."""
        logging.basicConfig(
            level=logging.INFO,
            format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
            handlers=[
                logging.StreamHandler(sys.stdout),
                logging.FileHandler(self.base_dir / 'dependency_manager.log')
            ]
        )
        self.logger = logging.getLogger('DependencyManager')

    def _load_config(self) -> Dict:
        """Load and validate the YAML configuration file."""
        try:
            with open(self.config_path, 'r') as f:
                config = yaml.safe_load(f)

            if 'thirdpartylibs' not in config:
                raise ValueError("Configuration must contain 'thirdpartylibs' section")

            self.logger.info(f"Loaded configuration from {self.config_path}")
            return config

        except FileNotFoundError:
            self.logger.error(f"Configuration file not found: {self.config_path}")
            raise
        except yaml.YAMLError as e:
            self.logger.error(f"Error parsing YAML configuration: {e}")
            raise
        except Exception as e:
            self.logger.error(f"Unexpected error loading configuration: {e}")
            raise

    def _run_git_command(self, cmd: List[str], cwd: Optional[Path] = None) -> subprocess.CompletedProcess:
        """
        Run a git command and return the result.

        Args:
            cmd: Git command as a list of arguments
            cwd: Working directory for the command

        Returns:
            CompletedProcess object with the result
        """
        try:
            result = subprocess.run(
                cmd,
                cwd=cwd,
                capture_output=True,
                text=True,
                check=True
            )
            return result
        except subprocess.CalledProcessError as e:
            self.logger.error(f"Git command failed: {' '.join(cmd)}")
            self.logger.error(f"Error output: {e.stderr}")
            raise

    def _get_current_git_info(self, repo_path: Path) -> Optional[Dict[str, str]]:
        """
        Get current git information for a repository.

        Args:
            repo_path: Path to the git repository

        Returns:
            Dictionary with git information or None if not a git repo
        """
        if not (repo_path / '.git').exists():
            return None

        try:
            # Get current commit hash
            hash_result = self._run_git_command(['git', 'rev-parse', 'HEAD'], repo_path)
            current_hash = hash_result.stdout.strip()

            # Get current branch (if on a branch)
            try:
                branch_result = self._run_git_command(['git', 'rev-parse', '--abbrev-ref', 'HEAD'], repo_path)
                current_branch = branch_result.stdout.strip()
                if current_branch == 'HEAD':
                    current_branch = None  # Detached HEAD
            except subprocess.CalledProcessError:
                current_branch = None

            # Get remote URL
            try:
                remote_result = self._run_git_command(['git', 'config', '--get', 'remote.origin.url'], repo_path)
                remote_url = remote_result.stdout.strip()
            except subprocess.CalledProcessError:
                remote_url = None

            return {
                'hash': current_hash,
                'branch': current_branch,
                'remote_url': remote_url
            }

        except subprocess.CalledProcessError:
            return None

    def _load_stamp_file(self, library_name: str) -> Optional[Dict[str, str]]:
        """
        Load stamp file for a library.

        Args:
            library_name: Name of the library

        Returns:
            Dictionary with stamp information or None if not found
        """
        stamp_file = self.stamp_dir / f".stamp_{library_name}"

        if not stamp_file.exists():
            return None

        try:
            stamp_data = {}
            with open(stamp_file, 'r') as f:
                for line in f:
                    line = line.strip()
                    if '=' in line and not line.startswith('#'):
                        key, value = line.split('=', 1)
                        stamp_data[key.strip()] = value.strip()
            return stamp_data
        except Exception as e:
            self.logger.warning(f"Error reading stamp file for {library_name}: {e}")
            return None

    def _write_stamp_file(self, library_name: str, repo_url: str, checkout_ref: str, git_hash: str):
        """
        Write stamp file for a library.

        Args:
            library_name: Name of the library
            repo_url: Repository URL
            checkout_ref: Branch, tag, or commit that was checked out
            git_hash: Actual git hash of the checkout
        """
        stamp_file = self.stamp_dir / f".stamp_{library_name}"

        try:
            with open(stamp_file, 'w') as f:
                f.write(f"# Uefinity Dependency Stamp File\n")
                f.write(f"# Generated on {datetime.now().isoformat()}\n")
                f.write(f"#\n")
                f.write(f"repository_url={repo_url}\n")
                f.write(f"checkout_ref={checkout_ref}\n")
                f.write(f"git_hash={git_hash}\n")
                f.write(f"checkout_date={datetime.now().isoformat()}\n")

            self.logger.info(f"Created stamp file for {library_name}")

        except Exception as e:
            self.logger.error(f"Error writing stamp file for {library_name}: {e}")
            raise

    def _determine_checkout_type(self, checkout_ref: str) -> str:
        """
        Determine if checkout_ref is a branch, tag, or commit hash.

        Args:
            checkout_ref: Reference to check

        Returns:
            Type of reference: 'branch', 'tag', or 'commit'
        """
        # Simple heuristic: if it looks like a commit hash (40 hex chars), treat as commit
        if len(checkout_ref) == 40 and all(c in '0123456789abcdef' for c in checkout_ref.lower()):
            return 'commit'

        # If it starts with 'v' and contains dots, likely a tag
        if checkout_ref.startswith('v') and '.' in checkout_ref:
            return 'tag'

        # Default to branch
        return 'branch'

    def _checkout_dependency(self, library_name: str, config: Dict[str, Union[str, Dict]]) -> bool:
        """
        Checkout a single dependency.

        Args:
            library_name: Name of the library
            config: Configuration for this library

        Returns:
            True if successful, False otherwise
        """
        repo_url = config['repository']
        checkout_ref = config.get('branch', config.get('tag', config.get('commit', 'master')))
        target_dir = config.get('directory', library_name)

        repo_path = self.thirdparty_dir / target_dir

        self.logger.info(f"Processing {library_name}...")

        # Check if already cloned
        if repo_path.exists() and (repo_path / '.git').exists():
            # Repository exists, check if we need to update
            current_info = self._get_current_git_info(repo_path)
            stamp_info = self._load_stamp_file(library_name)

            needs_update = self.force

            if not needs_update and stamp_info:
                # Check if current state matches stamp
                if (stamp_info.get('repository_url') != repo_url or
                    stamp_info.get('checkout_ref') != checkout_ref):
                    needs_update = True
                    self.logger.info(f"{library_name}: Configuration changed, updating...")
                elif current_info and current_info['hash'] != stamp_info.get('git_hash'):
                    needs_update = True
                    self.logger.info(f"{library_name}: Repository state changed, updating...")
            elif not stamp_info:
                needs_update = True
                self.logger.info(f"{library_name}: No stamp file found, updating...")

            if not needs_update:
                self.logger.info(f"{library_name}: Up to date")
                return True

            # Update existing repository
            try:
                self.logger.info(f"{library_name}: Fetching updates...")
                self._run_git_command(['git', 'fetch', '--all'], repo_path)

                # Determine checkout type and checkout accordingly
                checkout_type = self._determine_checkout_type(checkout_ref)

                if checkout_type == 'commit':
                    self._run_git_command(['git', 'checkout', checkout_ref], repo_path)
                elif checkout_type == 'tag':
                    self._run_git_command(['git', 'checkout', f'tags/{checkout_ref}'], repo_path)
                else:  # branch
                    self._run_git_command(['git', 'checkout', f'origin/{checkout_ref}'], repo_path)

                # Update submodules
                self._run_git_command(['git', 'submodule', 'update', '--init', '--recursive'], repo_path)

            except subprocess.CalledProcessError as e:
                self.logger.error(f"Failed to update {library_name}: {e}")
                return False

        else:
            # Clone new repository
            try:
                self.logger.info(f"{library_name}: Cloning from {repo_url}...")

                if repo_path.exists():
                    # Remove existing non-git directory
                    subprocess.run(['rm', '-rf', str(repo_path)], check=True)

                self._run_git_command(['git', 'clone', '--recursive', repo_url, str(repo_path)])

                # Checkout specific reference if not master/main
                if checkout_ref not in ['master', 'main']:
                    checkout_type = self._determine_checkout_type(checkout_ref)

                    if checkout_type == 'commit':
                        self._run_git_command(['git', 'checkout', checkout_ref], repo_path)
                    elif checkout_type == 'tag':
                        self._run_git_command(['git', 'checkout', f'tags/{checkout_ref}'], repo_path)
                    else:  # branch
                        self._run_git_command(['git', 'checkout', f'origin/{checkout_ref}'], repo_path)

                # Initialize/update submodules
                self._run_git_command(['git', 'submodule', 'update', '--init', '--recursive'], repo_path)

            except subprocess.CalledProcessError as e:
                self.logger.error(f"Failed to clone {library_name}: {e}")
                return False

        # Get final git hash and write stamp file
        try:
            final_info = self._get_current_git_info(repo_path)
            if final_info:
                self._write_stamp_file(library_name, repo_url, checkout_ref, final_info['hash'])
                self.logger.info(f"{library_name}: Successfully checked out {checkout_ref} ({final_info['hash'][:8]})")
                return True
            else:
                self.logger.error(f"Failed to get git info for {library_name}")
                return False

        except Exception as e:
            self.logger.error(f"Error finalizing checkout for {library_name}: {e}")
            return False

    def manage_dependencies(self) -> bool:
        """
        Manage all dependencies according to configuration.

        Returns:
            True if all dependencies were processed successfully
        """
        libraries = self.config['thirdpartylibs']

        if not libraries:
            self.logger.info("No third-party libraries configured")
            return True

        self.logger.info(f"Managing {len(libraries)} third-party libraries...")

        success_count = 0
        total_count = len(libraries)

        for library_name, library_config in libraries.items():
            try:
                if self._checkout_dependency(library_name, library_config):
                    success_count += 1
                else:
                    self.logger.error(f"Failed to process {library_name}")

            except Exception as e:
                self.logger.error(f"Unexpected error processing {library_name}: {e}")

        self.logger.info(f"Dependency management complete: {success_count}/{total_count} successful")
        return success_count == total_count


def main():
    """Main entry point for the dependency manager."""
    parser = argparse.ArgumentParser(
        description="Uefinity Dependency Management System",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s --config deps.yaml
  %(prog)s --config deps.yaml --force
  %(prog)s --config /path/to/deps.yaml --base-dir /path/to/project
        """
    )

    parser.add_argument(
        '--config',
        default='deps.yaml',
        help='Path to YAML configuration file (default: deps.yaml)'
    )

    parser.add_argument(
        '--base-dir',
        default='.',
        help='Base directory for the project (default: current directory)'
    )

    parser.add_argument(
        '--force',
        action='store_true',
        help='Force re-checkout of all dependencies'
    )

    parser.add_argument(
        '--version',
        action='version',
        version='Uefinity Dependency Manager 1.0.0'
    )

    args = parser.parse_args()

    try:
        # Convert to absolute paths
        config_path = os.path.abspath(args.config)
        base_dir = os.path.abspath(args.base_dir)

        # Create dependency manager and run
        manager = DependencyManager(config_path, base_dir, args.force)
        success = manager.manage_dependencies()

        sys.exit(0 if success else 1)

    except KeyboardInterrupt:
        print("\nOperation cancelled by user")
        sys.exit(130)
    except Exception as e:
        print(f"Fatal error: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == '__main__':
    main()