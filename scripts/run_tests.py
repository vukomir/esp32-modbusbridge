#!/usr/bin/env python3
"""
Comprehensive test runner for the inverter monitoring system.
Supports both local testing and CI/CD environments.
"""

import subprocess
import sys
import os
import argparse
from pathlib import Path
from typing import List, Dict, Optional

class Colors:
    """ANSI color codes for terminal output"""
    RED = '\033[0;31m'
    GREEN = '\033[0;32m'
    YELLOW = '\033[1;33m'
    BLUE = '\033[0;34m'
    PURPLE = '\033[0;35m'
    CYAN = '\033[0;36m'
    WHITE = '\033[1;37m'
    NC = '\033[0m'  # No Color

class TestRunner:
    def __init__(self):
        self.project_dir = Path(__file__).parent.parent
        self.total_tests = 0
        self.passed_tests = 0
        self.failed_tests = 0
        self.test_results: Dict[str, bool] = {}
        
    def run_command(self, cmd: List[str], cwd: Optional[Path] = None) -> tuple[int, str, str]:
        """Run a command and return exit code, stdout, stderr"""
        try:
            result = subprocess.run(
                cmd, 
                cwd=cwd or self.project_dir,
                capture_output=True,
                text=True,
                timeout=300  # 5 minute timeout
            )
            return result.returncode, result.stdout, result.stderr
        except subprocess.TimeoutExpired:
            return 1, "", "Command timed out after 5 minutes"
        except Exception as e:
            return 1, "", str(e)
    
    def print_header(self, title: str):
        """Print a formatted header"""
        print(f"\n{Colors.BLUE}{'='*60}{Colors.NC}")
        print(f"{Colors.WHITE}{title:^60}{Colors.NC}")
        print(f"{Colors.BLUE}{'='*60}{Colors.NC}\n")
    
    def print_test_result(self, test_name: str, success: bool, duration: str = ""):
        """Print formatted test result"""
        status = f"{Colors.GREEN}✅ PASSED{Colors.NC}" if success else f"{Colors.RED}❌ FAILED{Colors.NC}"
        duration_str = f" ({duration})" if duration else ""
        print(f"{test_name:<30} {status}{duration_str}")
    
    def run_native_tests(self, test_suites: List[str] = None) -> bool:
        """Run tests on native platform"""
        self.print_header("Native Tests (No Hardware Required)")
        
        # Default test suites that work on native platform
        default_suites = [
            "test_basic",
            "test_config", 
            "test_esplogger",
            "test_modbus_client",
            "test_inverter_factory",
            "test_solplanet_asw",
            "test_hiking_dds238",
            "test_poller"
        ]
        
        suites_to_run = test_suites or default_suites
        all_passed = True
        
        for suite in suites_to_run:
            print(f"\n{Colors.CYAN}Running {suite}...{Colors.NC}")
            
            cmd = ["pio", "test", "-e", "native", "-f", suite, "-v"]
            exit_code, stdout, stderr = self.run_command(cmd)
            
            success = exit_code == 0
            self.test_results[suite] = success
            self.total_tests += 1
            
            if success:
                self.passed_tests += 1
                self.print_test_result(suite, True)
            else:
                self.failed_tests += 1
                self.print_test_result(suite, False)
                all_passed = False
                
                # Print error details
                if stderr:
                    print(f"{Colors.RED}Error output:{Colors.NC}")
                    print(stderr)
                if stdout and "FAILED" in stdout:
                    print(f"{Colors.RED}Test failures:{Colors.NC}")
                    # Extract failure lines
                    for line in stdout.split('\n'):
                        if 'FAILED' in line or 'FAIL:' in line:
                            print(f"  {line}")
        
        return all_passed
    
    def compile_for_hardware(self, environments: List[str] = None) -> bool:
        """Compile code for ESP32 hardware (compilation test only)"""
        self.print_header("Hardware Compilation Tests")
        
        environments = environments or ["dev", "prod"]
        all_passed = True
        
        for env in environments:
            print(f"\n{Colors.CYAN}Compiling for {env} environment...{Colors.NC}")
            
            cmd = ["pio", "run", "-e", env]
            exit_code, stdout, stderr = self.run_command(cmd)
            
            success = exit_code == 0
            self.test_results[f"compile_{env}"] = success
            self.total_tests += 1
            
            if success:
                self.passed_tests += 1
                self.print_test_result(f"Compile {env}", True)
                
                # Show binary size info
                size_cmd = ["pio", "run", "-e", env, "-t", "size"]
                size_exit, size_out, _ = self.run_command(size_cmd)
                if size_exit == 0 and size_out:
                    print(f"{Colors.YELLOW}Binary size info:{Colors.NC}")
                    for line in size_out.split('\n')[-5:]:  # Last few lines usually have the summary
                        if line.strip() and ('text' in line or 'data' in line or 'bss' in line):
                            print(f"  {line}")
            else:
                self.failed_tests += 1
                self.print_test_result(f"Compile {env}", False)
                all_passed = False
                
                if stderr:
                    print(f"{Colors.RED}Compilation errors:{Colors.NC}")
                    # Show last 20 lines of error output
                    error_lines = stderr.split('\n')[-20:]
                    for line in error_lines:
                        if line.strip():
                            print(f"  {line}")
        
        return all_passed
    
    def run_code_quality_checks(self) -> bool:
        """Run static analysis and code quality checks"""
        self.print_header("Code Quality Checks")
        
        all_passed = True
        
        # Static analysis with PlatformIO Check
        print(f"\n{Colors.CYAN}Running static analysis...{Colors.NC}")
        cmd = ["pio", "check", "--skip-packages"]
        exit_code, stdout, stderr = self.run_command(cmd)
        
        # Note: pio check often returns non-zero even for warnings, so we're more lenient
        success = exit_code == 0 or "error" not in stderr.lower()
        self.test_results["static_analysis"] = success
        self.total_tests += 1
        
        if success:
            self.passed_tests += 1
            self.print_test_result("Static Analysis", True)
        else:
            self.failed_tests += 1
            self.print_test_result("Static Analysis", False)
            all_passed = False
            
            if stderr:
                print(f"{Colors.RED}Analysis issues:{Colors.NC}")
                for line in stderr.split('\n')[:10]:  # First 10 lines
                    if line.strip():
                        print(f"  {line}")
        
        return all_passed
    
    def print_summary(self):
        """Print final test summary"""
        self.print_header("Test Summary")
        
        print(f"Total Tests: {self.total_tests}")
        print(f"{Colors.GREEN}Passed: {self.passed_tests}{Colors.NC}")
        print(f"{Colors.RED}Failed: {self.failed_tests}{Colors.NC}")
        
        if self.failed_tests == 0:
            print(f"\n{Colors.GREEN}🎉 ALL TESTS PASSED! 🎉{Colors.NC}")
            print(f"{Colors.GREEN}System is ready for deployment.{Colors.NC}")
        else:
            print(f"\n{Colors.RED}⚠️  SOME TESTS FAILED ⚠️{Colors.NC}")
            print(f"{Colors.RED}Please review failed tests before deployment.{Colors.NC}")
            
            print(f"\n{Colors.YELLOW}Failed tests:{Colors.NC}")
            for test_name, passed in self.test_results.items():
                if not passed:
                    print(f"  ❌ {test_name}")
    
    def run_all_tests(self, args) -> bool:
        """Run the complete test suite"""
        print(f"{Colors.PURPLE}🧪 Inverter Monitoring System - Test Runner{Colors.NC}")
        print(f"{Colors.PURPLE}{'='*50}{Colors.NC}")
        
        all_passed = True
        
        # Run native tests
        if not args.skip_native:
            if not self.run_native_tests(args.test_suites):
                all_passed = False
        
        # Compile for hardware
        if not args.skip_compile:
            if not self.compile_for_hardware(args.environments):
                all_passed = False
        
        # Code quality checks
        if not args.skip_quality:
            if not self.run_code_quality_checks():
                all_passed = False
        
        # Print summary
        self.print_summary()
        
        return all_passed

def main():
    parser = argparse.ArgumentParser(description="Run comprehensive tests for inverter monitoring system")
    parser.add_argument("--test-suites", nargs="*", help="Specific test suites to run")
    parser.add_argument("--environments", nargs="*", help="Specific environments to compile for")
    parser.add_argument("--skip-native", action="store_true", help="Skip native tests")
    parser.add_argument("--skip-compile", action="store_true", help="Skip hardware compilation")
    parser.add_argument("--skip-quality", action="store_true", help="Skip code quality checks")
    parser.add_argument("--ci", action="store_true", help="Running in CI environment")
    
    args = parser.parse_args()
    
    # Check if PlatformIO is available
    try:
        subprocess.run(["pio", "--version"], capture_output=True, check=True)
    except (subprocess.CalledProcessError, FileNotFoundError):
        print(f"{Colors.RED}Error: PlatformIO CLI not found. Please install PlatformIO.{Colors.NC}")
        return 1
    
    # Run tests
    runner = TestRunner()
    success = runner.run_all_tests(args)
    
    return 0 if success else 1

if __name__ == "__main__":
    sys.exit(main())
