# Maintainer: Jakub Klinkovský <j.l.k@gmx.com>

pkgname=smallterm-git
_pkgname=smallterm
pkgver=0.2.6.ga28969d
pkgrel=1
pkgdesc="Very lightweight terminal emulator based on VTE (fork of tinyterm-svn package)"
arch=('i686' 'x86_64')
url="https://github.com/laelath/tinyterm"
license=('MIT')
depends=('vte3')
makedepends=('git')
source=('git://github.com/laelath/tinyterm.git')
md5sums=('SKIP')

pkgver() {
  cd "$_pkgname"
  git describe --long | sed 's|^v||;s|-|.|g'
}

build() {
  cd "$_pkgname"
  make
}

package() {
  cd "$_pkgname"
  make DESTDIR="$pkgdir" install
  install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}

# vim:set ts=2 sw=2 et:
