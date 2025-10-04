#pragma once

BOOLEAN DriveMapIsValidDriveString(PCSTR DriveString);
UCHAR   DriveMapGetBiosDriveNumber(PCSTR DeviceName);
VOID    DriveMapMapDrivesInSection(ULONG_PTR SectionId);
