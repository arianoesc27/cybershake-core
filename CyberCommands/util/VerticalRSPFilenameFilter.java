package util;

import java.io.File;
import java.io.FilenameFilter;

//AR: This mirrors RotDFilenameFilter, but for DirectSynth's
//AR: "VerticalRSP_<site>_<run>_<src>_<rup>.rsp" files, the vertical (Z) component response
//AR: spectrum. Checking both the prefix and the suffix, not just one of them, avoids
//AR: accidentally picking up some unrelated file that happens to end in ".rsp".
public class VerticalRSPFilenameFilter implements FilenameFilter {

	public boolean accept(File dir, String name) {
		if (name.startsWith("VerticalRSP") && name.endsWith(".rsp")) {
			return true;
		}
		return false;
	}

}
