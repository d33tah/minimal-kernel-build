#!/bin/bash
# Find all unused header files in minified/include/

cd /home/user/minimal-kernel-build/minified

echo "Finding potentially unused headers in all include subdirectories..."
echo ""

# Get all headers in include/ (excluding generated/)
for header in $(find include -name "*.h" -not -path "include/generated/*" | sort); do
    header_rel=$(echo "$header" | sed 's|^include/||')
    header_name=$(basename "$header")

    # Search for includes of this header in all .c and .h files
    found=0

    # Try different include patterns
    if grep -r "^\s*#\s*include\s*[<\"]${header_rel}[>\"]" --include="*.c" --include="*.h" . >/dev/null 2>&1; then
        found=1
    fi
    
    # Also try without the include/ prefix (for backwards compat)
    if [ $found -eq 0 ]; then
        if grep -r "^\s*#\s*include\s*[<\"]${header_name}[>\"]" --include="*.c" --include="*.h" . >/dev/null 2>&1; then
            found=1
        fi
    fi

    # If not found anywhere, report it
    if [ $found -eq 0 ]; then
        lines=$(wc -l < "$header")
        echo "${header} (${lines} lines)"
    fi
done
