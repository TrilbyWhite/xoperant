
pkgname=libphidget
pkgver=2.1.8.20130320
pkgrel=1
pkgdesc="A library for Phidgets sensors and controllers"
arch=('i686' 'x86_64')
url="http://www.phidgets.com/"
license=('LGPL3')
depends=('libusb-compat');
makedepends=()
source=("http://www.phidgets.com/downloads/libraries/libphidget.tar.gz")
sha256sums=('60660f1d4ec278ed6a853e9804dccf42558511ffa6d79d2ac35215d7c2cf2b26')

build() {
  cd "${srcdir}/${pkgname}-${pkgver}"
  ./configure --prefix=/usr --disable-jni
  make
}

package() {
  cd "${srcdir}/${pkgname}-${pkgver}"
  install -d "${pkgdir}/etc/udev/rules.d/"
  make DESTDIR="${pkgdir}" install
  install -m 644 udev/99-phidgets.rules "${pkgdir}/etc/udev/rules.d/"
}
