#ifndef _LINUX_PHY_H
#define _LINUX_PHY_H

#define PHY_INTERFACE_MODE_MAX 20

static inline const char *phy_modes(int mode)
{
	static const char *modes[] = {
		"internal", "mii", "gmii", "sgmii", "qsgmii", "tsgmii",
		"rgmii", "rgmii-id", "rgmii-rxid", "rgmii-txid", "rtbi",
		"smii", "xgmii", "trgmii", "1000base-x", "2500base-x",
		"rxaui", "xaui", "10gbase-kr", "unknown"
	};
	if (mode >= 0 && mode < PHY_INTERFACE_MODE_MAX)
		return modes[mode];
	return "unknown";
}

#endif /* _LINUX_PHY_H */