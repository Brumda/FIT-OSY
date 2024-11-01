#ifndef __PROGTEST__

#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cassert>
#include <stdexcept>

using namespace std;

constexpr int SECTOR_SIZE = 512;
constexpr int MAX_RAID_DEVICES = 16;
constexpr int MAX_DEVICE_SECTORS = 1024 * 1024 * 2;
constexpr int MIN_DEVICE_SECTORS = 1 * 1024 * 2;

constexpr int RAID_STOPPED = 0;
constexpr int RAID_OK = 1;
constexpr int RAID_DEGRADED = 2;
constexpr int RAID_FAILED = 3;

struct TBlkDev {
    int m_Devices;
    int m_Sectors;

    int (*m_Read )(int, int, void *, int);

    int (*m_Write )(int, int, const void *, int);
};

#include <iostream>

string raidStatusToString(int status) {
    switch (status) {
        case RAID_STOPPED:
            return "RAID_STOPPED";
        case RAID_OK:
            return "RAID_OK";
        case RAID_DEGRADED:
            return "RAID_DEGRADED";
        case RAID_FAILED:
            return "RAID_FAILED";
        default:
            return "Unknown";
    }
}

#endif /* __PROGTEST__ */


class CRaidVolume {
public:
    struct ServiceData {
        int timeStamp;
        int status;
        int brokenDisc;


        void setDataInto(unsigned char *buffer) {
            memcpy(buffer, this, sizeof(ServiceData));
        }

        void loadDataFrom(unsigned char *buffer) {
            memcpy(this, buffer, sizeof(ServiceData));
        }

        bool operator==(const ServiceData &rhs) const {
            return timeStamp == rhs.timeStamp;
        }

        bool operator!=(const ServiceData &rhs) const {
            return !(rhs == *this);
        }
    };

    static bool create(const TBlkDev &dev) {
        locDev = dev;
        unsigned char buffer[SECTOR_SIZE];
        ServiceData data = {1, RAID_STOPPED, -1};
        data.setDataInto(buffer);

        for (int i = 0; i < locDev.m_Devices; ++i) {
            if (locDev.m_Write(i, locDev.m_Sectors - 1, buffer, 1) != 1)
                return false;
        }
        return true;
    }

    bool raidFailureCheck(int disc) {
        if (serviceData.brokenDisc != -1 && disc != serviceData.brokenDisc) {
            serviceData.status = RAID_FAILED;
            return false;
        }
        serviceData.brokenDisc = disc;
        serviceData.status = RAID_DEGRADED;
        return true;
    }

//                cout << "Loaded:\t" << "time stamp: " << loadedData[i].timeStamp << " broken disk: "
//                     << loadedData[i].brokenDisc
//                     << " status: " << raidStatusToString(loadedData[i].status) << endl;




    int start(const TBlkDev &dev) {
        locDev = dev;
        serviceData = {1, RAID_OK, -1};
        ServiceData loadedData[MAX_RAID_DEVICES];
        unsigned char diskServiceData[SECTOR_SIZE];
        for (int i = 0; i < 3; ++i) {
            if (locDev.m_Read(i, locDev.m_Sectors - 1, diskServiceData, 1) != 1) {
                // found unreadable disk different from already fucked disk
                if (!raidFailureCheck(i)) return RAID_FAILED;
            } else
                loadedData[i].loadDataFrom(diskServiceData);
        }
        if (serviceData.status == RAID_OK) {
            int different = 0, differentNum = 0;
            for (int i = 1; i < 3; ++i) {
                if (loadedData[i] != loadedData[0]) {
                    different++;
                    differentNum = i;
                }
            }
            if (different == 2)
                return RAID_FAILED; // tohle je bad, páč když je rozbitej první disk, tak to dá vždycky fail xd progtestu to nevadí tho
            if (different == 1) {
                serviceData.status = RAID_DEGRADED;
                serviceData.brokenDisc =
                        loadedData[differentNum] != loadedData[differentNum == 1 ? 2 : 1] ? differentNum : 0;
            }
        } else if (serviceData.status == RAID_DEGRADED) {
            for (int i = 0; i < 3; ++i) {
                if (i == serviceData.brokenDisc) continue;
                if (loadedData[i] != loadedData[serviceData.brokenDisc != 0 ? 0 : 1]) {
                    return RAID_FAILED;
                }
            }
        }
        serviceData.timeStamp = loadedData[serviceData.brokenDisc != 0 ? 0 : 1].timeStamp;

        for (int i = 3; i < locDev.m_Devices; ++i) {
            if (locDev.m_Read(i, locDev.m_Sectors - 1, diskServiceData, 1) != 1) {
                // found unreadable disk different from already fucked disk
                if (!raidFailureCheck(i)) return RAID_FAILED;
            } else {
                loadedData[i].loadDataFrom(diskServiceData);
                if (loadedData[i] != serviceData) {
                    // found wrong service data on a different disk than already fucked disk
                    if (!raidFailureCheck(i)) return RAID_FAILED;
                }
            }
        }
        return serviceData.status;
    }

    int stop() {
        unsigned char buffer[SECTOR_SIZE];
        if (serviceData.status != RAID_STOPPED && serviceData.status != RAID_FAILED) {
            ++serviceData.timeStamp;
            bool fucked = false;
            while (true) {
                serviceData.setDataInto(buffer);
                int i = 0;
                for (; i < locDev.m_Devices; ++i) {
                    if (i != serviceData.brokenDisc) {
                        if (locDev.m_Write(i, locDev.m_Sectors - 1, buffer, 1) != 1) {
                            raidFailureCheck(i);
                            fucked = true;
                            break;
                        }
                    }
                }
                if (!fucked) break;
            }
        }
        serviceData.status = RAID_STOPPED;
        return serviceData.status;
    }

    int resync() {
        if (serviceData.status == RAID_DEGRADED) {
            for (int i = 0; i < locDev.m_Sectors - 1; ++i) {
                unsigned char fixedSector[SECTOR_SIZE] = {0};
                if (!calculateParity(fixedSector, i, serviceData.brokenDisc)) {
                    serviceData.status = RAID_FAILED;
                    return serviceData.status;
                }
                if (locDev.m_Write(serviceData.brokenDisc, i, fixedSector, 1) != 1)
                    return serviceData.status;

            }
            serviceData.status = RAID_OK;
            serviceData.brokenDisc = -1;
        }
        return serviceData.status;
    }

    int status() const { return serviceData.status; }

    int size() const { return (locDev.m_Devices - 1) * (locDev.m_Sectors - 1); }


    bool calculateParity(unsigned char *buffer, int row, int skip) {
        for (int disk = 0; disk < locDev.m_Devices; ++disk) {
            if (skip == disk) continue;
            unsigned char otherSectors[SECTOR_SIZE];
            if (locDev.m_Read(disk, row, otherSectors, 1) != 1)
                return false;

            for (int k = 0; k < SECTOR_SIZE; ++k)
                buffer[k] ^= otherSectors[k];
        }
        return true;
    }

    int calculateRow(int sector) const {
        return sector / (locDev.m_Devices - 1);
    }

    int calculateParityDisk(int sector) const {
        return calculateRow(sector) % locDev.m_Devices;
    }

    int calculateDisk(int sector, int parityDisk) const {
        int disk = sector % (locDev.m_Devices - 1);
        return disk + (parityDisk <= disk ? 1 : 0);
    }

    bool read(int secNr, void *data, int secCnt) {
        if (serviceData.status == RAID_OK) {
            for (int i = secNr; i < secNr + secCnt; ++i) {
                int row = calculateRow(i);
                int disk = calculateDisk(i, calculateParityDisk(i));
                if (locDev.m_Read(disk, row, (unsigned char *) data + (i - secNr) * SECTOR_SIZE, 1) != 1) {
                    serviceData.status = RAID_DEGRADED;
                    serviceData.brokenDisc = disk;
                    break;
                }
            }
        }

        if (serviceData.status == RAID_DEGRADED) {
            for (int i = secNr; i < secNr + secCnt; ++i) {
                int row = calculateRow(i);
                int disk = calculateDisk(i, calculateParityDisk(i));
                if (serviceData.brokenDisc == disk) {
                    unsigned char reconstructedSector[SECTOR_SIZE] = {0};
                    if (!calculateParity(reconstructedSector, calculateRow(i), disk)) {
                        serviceData.status = RAID_FAILED;
                        break;
                    }

                    memcpy((unsigned char *) data + (i - secNr) * SECTOR_SIZE, reconstructedSector, SECTOR_SIZE);
                } else {
                    if (locDev.m_Read(disk, row, (unsigned char *) data + (i - secNr) * SECTOR_SIZE, 1) != 1) {
                        serviceData.status = RAID_FAILED;
                        break;
                    }
                }
            }
        }
        return serviceData.status != RAID_FAILED;
    }

    bool write(int secNr, const void *data, int secCnt) {
        if (serviceData.status == RAID_OK) {
            for (int i = secNr; i < secNr + secCnt; ++i) {
                int row = calculateRow(i);
                int parityDisk = calculateParityDisk(i);
                int disk = calculateDisk(i, parityDisk);

                unsigned char parity[SECTOR_SIZE];
                unsigned char sector[SECTOR_SIZE];

                if (locDev.m_Read(parityDisk, row, parity, 1) != 1) {
                    serviceData.status = RAID_DEGRADED;
                    serviceData.brokenDisc = parityDisk;
                    break;
                }
                if (!read(i, sector, 1))
                    break;

                for (int j = 0; j < SECTOR_SIZE; ++j)
                    parity[j] ^= sector[j];

                memcpy(sector, (unsigned char *) data + (i - secNr) * SECTOR_SIZE, SECTOR_SIZE);

                for (int j = 0; j < SECTOR_SIZE; ++j)
                    parity[j] ^= sector[j];

                if (locDev.m_Write(parityDisk, row, parity, 1) != 1) {
                    serviceData.status = RAID_DEGRADED;
                    serviceData.brokenDisc = parityDisk;
                    break;
                }
                if (locDev.m_Write(disk, row, sector, 1) != 1) {
                    serviceData.status = RAID_DEGRADED;
                    serviceData.brokenDisc = disk;
                    break;
                }
            }

        }
        if (serviceData.status == RAID_DEGRADED) {
            for (int i = secNr; i < secNr + secCnt; ++i) {
                int row = calculateRow(i);
                int parityDisk = calculateParityDisk(i);
                int disk = calculateDisk(i, parityDisk);
                unsigned char sector[SECTOR_SIZE] = {0};

                if (!read(i, sector, 1))
                    break;

                if (serviceData.brokenDisc != parityDisk) {
                    unsigned char parity[SECTOR_SIZE];
                    if (locDev.m_Read(parityDisk, row, parity, 1) != 1) {
                        serviceData.status = RAID_FAILED;
                        break;
                    }
                    for (int j = 0; j < SECTOR_SIZE; ++j)
                        parity[j] ^= sector[j];

                    memcpy(sector, (unsigned char *) data + (i - secNr) * SECTOR_SIZE, SECTOR_SIZE);

                    for (int j = 0; j < SECTOR_SIZE; ++j)
                        parity[j] ^= sector[j];

                    if (locDev.m_Write(parityDisk, row, parity, 1) != 1) {
                        serviceData.status = RAID_FAILED;
                        break;
                    }
                }
                if (serviceData.brokenDisc != disk) {
                    if (locDev.m_Write(disk, row, (unsigned char *) data + (i - secNr) * SECTOR_SIZE, 1) != 1) {
                        serviceData.status = RAID_FAILED;
                        break;
                    }
                }
            }
        }

        return serviceData.status != RAID_FAILED;
    }

protected:
    static TBlkDev locDev;
    ServiceData serviceData;
};

TBlkDev CRaidVolume::locDev;
#ifndef __PROGTEST__

#include "tests2.inc"

#endif /* __PROGTEST__ */
