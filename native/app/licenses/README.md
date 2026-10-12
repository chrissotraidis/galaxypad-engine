# License texts shipped with GalaxyPadEngine.framework

The iOS framework build copies the license text of everything it links into
`GalaxyPadEngine.framework/Licenses` (native/app/CMakeLists.txt). Most texts come
from the fetched sources themselves. Dawn's prebuilt package has no license file,
so `Dawn.txt` is Dawn's `LICENSE` at Aurora's pinned Dawn commit
(`1155e0ed531126f33a1279afa029349651ca1c93`); update it with that pin.
