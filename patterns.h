#pragma once

#define LE1_ExecutableName L"MassEffect1.exe"

#define LE1_UFunctionBind_Pattern (BYTE*)"\x48\x8B\xC4\x55\x41\x56\x41\x57\x48\x8D\xA8\x78\xF8\xFF\xFF\x48\x81\xEC\x70\x08\x00\x00\x48\xC7\x44\x24\x50\xFE\xFF\xFF\xFF\x48\x89\x58\x10\x48\x89\x70\x18\x48\x89\x78\x20\x48\x8B\x00\x00\x00\x00\x00\x48\x33\xC4\x48\x89\x85\x60\x07\x00\x00\x48\x8B\xF1\xE8\x00\x00\x00\x00\x48\x8B\xF8\xF7\x86\xD8\x00\x00\x00\x00\x04\x00\x00"
#define LE1_UFunctionBind_Mask (BYTE*)"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx?????xxxxxxxxxxxxxx????xxxxxxxxxxxxx"

#define LE1_GetName_Pattern (BYTE*)"\x48\x8B\xC4\x48\x89\x50\x10\x57\x48\x83\xEC\x30\x48\xC7\x40\xF0\xFE\xFF\xFF\xFF\x48\x89\x58\x08\x48\x89\x68\x18\x48\x89\x70\x20\x48\x8B\xDA\x48\x8B\xF1\x33\xFF\x89\x78\xE8\x48\x89\x3A\x48\x89\x7A\x08\xC7\x40\xE8\x01\x00\x00\x00\x48\x63\x01\x48\x8D\x00\x00\x00\x00\x00\x85\xC0\x74\x23\x48\x8B\xC8\x48\xC1\xF8\x1D\x83\xE0\x07\x81\xE1\xFF\xFF\xFF\x1F\x48\x03\x4C\xC5\x00"
#define LE1_GetName_Mask (BYTE*)"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx?????xxxxxxxxxxxxxxxxxxxxxxxxx"
