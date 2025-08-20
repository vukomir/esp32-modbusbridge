#!/usr/bin/env python3
"""
Native build setup script for PlatformIO
Configures the build environment for native testing without ESP32 hardware
"""

import os
from pathlib import Path

Import("env")

def setup_native_build():
    """Configure the native build environment"""
    
    # Get project directory
    project_dir = Path(env.get("PROJECT_DIR"))
    
    # Add native mock sources to build
    mock_dir = project_dir / "test" / "native_mocks"
    if mock_dir.exists():
        env.Append(CPPPATH=[str(mock_dir)])
        
        # Add mock source files to build
        mock_sources = []
        for mock_file in mock_dir.glob("*.cpp"):
            mock_sources.append(str(mock_file))
            
        if mock_sources:
            env.Append(PIOBUILDFILES=mock_sources)
            print(f"Added native mock sources: {mock_sources}")
    
    # Configure native-specific build flags
    env.Append(CPPDEFINES=[
        ("NATIVE_BUILD", "1"),
        ("UNIT_TEST", "1"),
        ("ARDUINO", "100"),
        ("ESP32", "1"),
    ])
    
    # Suppress warnings for mock implementations
    env.Append(CCFLAGS=[
        "-Wno-unused-parameter",
        "-Wno-unused-variable", 
        "-Wno-missing-field-initializers",
        "-Wno-sign-compare",
    ])
    
    print("Native build environment configured")

def before_build(source, target, env):
    """Called before build starts"""
    setup_native_build()

# Only run for native environment
if env.get("PIOENV") == "native":
    env.AddPreAction("checkprogsize", before_build)
