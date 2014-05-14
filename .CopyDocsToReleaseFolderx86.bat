@echo off

rmdir /s /q "bin\DP_PB2 Rconpanel x86\"
mkdir "bin\DP_PB2 Rconpanel x86\"
mkdir "bin\DP_PB2 Rconpanel x86\Source\"

copy "additional files\*" "bin\DP_PB2 Rconpanel x86\"

copy "*.cpp" "bin\DP_PB2 Rconpanel x86\Source\"
copy "*.h" "bin\DP_PB2 Rconpanel x86\Source\"
copy "*.rc" "bin\DP_PB2 Rconpanel x86\Source\"
copy "*.ico" "bin\DP_PB2 Rconpanel x86\Source\"
copy "*.cbp" "bin\DP_PB2 Rconpanel x86\Source\"