This application is designed to let you adjust the inverse lens distortion paramaters in order to minimize visual artifacts (chromatic abbreation and barrel distortion) after you swap the lens on your VR headset. 

## This program must run in SteamVR's indrect mode:

Being in indirect mode is important because this allows us to bypass SteamVR and treat your HMD as a normal monitor. This means the distortion correction done by SteamVR is not applied and thus able to see the artifacts produced by your lens. This program recreates that same inverse distortion applied by SteamVR but allows you adjust it on the fly in order to correct for your specific lens.

To put your HMD into indirect mode go into the SteamVR settings Developers tab. NOTE: Enabling/Disabling direct mode in SteamVR can be finicky and you may need to reboot and/or unplug the HMD's HDMI cable in order for Windows to treat your HMD as a second monitor.

## How to Use:

**Keyboard Controls:**

		*  SPACEBAR - Toggles stauts overlay ON/OFF
		*  ENTER KEY - Toggle Linear Transforms ON/OFF/Aspect Ratio Only/Center Only

		*  Z/X: Toggle the LEFT and RIGHT eye ON/OFF when applying values
		*  1/2/3: Toggle on/off the 1st, 2nd, and 3rd coeffiecent
		*  Q/W/E: Toggle on/off the GREEN, BLUE, and RED colors
		*  LEFT and RIGHT arrow key: DECREASE and INCREASE the offset value to be applied
		*  UP and DOWN arrow keys: INCREASE and DECREASE all active options values 

		*  SHIFT + arrow keys: Move the center of projection by one pixel 
		*  CONTROL + arrow keys: Aspect Ratio (LEFT/RIGHT hortizinal UP/DOWN vertical) 
		*  I - Apply center correction to Intrensics
		*  NOTE: You can adjust the center without changing the Intrensics so you have to use "I" to actually apply these values

		*  G - Reset recenter (DOES NOT affect intrensics) for active eye
		*  H - Reset coeffiecents to 0.0 for all active eyes/colors/coefficents 
		*  J - Reset aspect ratio to default for all active eyes
		*  K - Reset recenter (DOES affect intrensics) for active eye 
	
		*  S/L: Save/Load state from JSON config file ( HMD_Config.json  ) 
		*  ESCAPE: Quit the application 

The ultimate goal of this application is to make the grid lines straight and white (with the exception of center axis lens that should remain green) as this means you have elimiated the barrel distorton and chromatic aberration of the lens.  

You need to adjust both eyes, all three colors, and all three coefficients to line them up properly as each lens will have slightly different characteristics. This means it's probably going to take some time to putz around and find the optimal values.

The first thing you want to adjust the center position for each eye so the two circles converge into one. Do this by holding SHIFT and hitting the arrow keys. Note: This center is not applied to the config file until you hit "I" key. I'm wasn't sure if it is wise to center by eye so I made it easy to not apply this value so you could easily keep what's already in your config file.

The you need to adjust three coefficent values per color (GREEN, BLUE, RED) per eye. You simply add or subtract (up/down key) an offset to each paramater and observe the change. You can adjust how large/small the offset is by using the LEFT/RIGHT arrow keys.

You can isolate a full eye, color, or individual coefficient that you want to make changes to by toggling them on/off. Use the status overlay screen (hit space bar to toggle on/off) to see what your active selection and current values is at any time.

S saves your changes

Once finished you load your config file into SteamVR via the [lighthouse_console tool and use this guide](https://www.reddit.com/r/Vive/comments/86uwsf/gearvr_to_vive_lens_adapters/dwdigxa/) if you don't know how to do that.
