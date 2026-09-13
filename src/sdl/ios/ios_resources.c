#include "ios_resources.h"
#include <string.h>

#include <CoreFoundation/CoreFoundation.h>

char *iOS_GetHomePath(void)
{
	char *home = NULL;
	CFURLRef homeURL = CFCopyHomeDirectoryURL();
	if (homeURL != NULL)
	{
		// The container root (homeURL) isn't visible in the Files app.
		// Only the Documents subfolder shows up under On My iPhone/iPad.
		CFURLRef docsURL = CFURLCreateCopyAppendingPathComponent(NULL, homeURL, CFSTR("Documents"), true);
		if (docsURL != NULL)
		{
			CFStringRef path = CFURLCopyFileSystemPath(docsURL, kCFURLPOSIXPathStyle);
			if (path != NULL)
			{
				CFIndex usedBytes = 0;
				CFStringGetBytes(path, CFRangeMake(0, CFStringGetLength(path)),
						kCFStringEncodingUTF8, 0, false, NULL, 0, &usedBytes);
				home = malloc(usedBytes + 1);
				CFStringGetCString(path, home, usedBytes + 1, kCFStringEncodingUTF8);
				CFRelease(path);
			}
			CFRelease(docsURL);
		}
		CFRelease(homeURL);
	}
	return home;
}

void iOS_GetResourcesPath(char * buffer)
{
	CFBundleRef mainBundle = CFBundleGetMainBundle();
	if (mainBundle != NULL)
	{
		CFURLRef resourcesUrl = CFBundleCopyResourcesDirectoryURL(mainBundle);
		if (resourcesUrl != NULL)
		{
			CFStringRef path = CFURLCopyFileSystemPath(resourcesUrl, kCFURLPOSIXPathStyle);
			if (path != NULL)
			{
				CFStringGetCString(path, buffer, 256, kCFStringEncodingUTF8);
				CFRelease(path);
			}
			CFRelease(resourcesUrl);
		}
	}
}
