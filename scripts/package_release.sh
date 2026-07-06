#!/usr/bin/env bash
set -euo pipefail

version="${1:-v0.1.0}"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
package_name="aliyun-sls-fluent-bit-plugin-${version}"
dist_dir="${repo_root}/dist"
tmp_dir="${dist_dir}/.tmp-${package_name}"
archive="${dist_dir}/${package_name}-src.tar.gz"
checksum_file="${archive}.sha256"
manifest="${dist_dir}/${package_name}-manifest.json"

cd "${repo_root}"

if ! git diff --quiet || ! git diff --cached --quiet; then
    echo "working tree has uncommitted changes; commit before packaging" >&2
    exit 1
fi

mkdir -p "${dist_dir}"
rm -rf "${tmp_dir}" "${archive}" "${checksum_file}" "${manifest}"

git archive --format=tar --prefix="${package_name}/" HEAD | gzip -n > "${archive}"

archive_sha256="$(sha256sum "${archive}" | awk '{print $1}')"
archive_file="$(basename "${archive}")"
commit="$(git rev-parse HEAD)"
created_at="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"

printf '%s  %s\n' "${archive_sha256}" "${archive_file}" > "${checksum_file}"

cat > "${manifest}" <<EOF
{
  "name": "aliyun-sls-fluent-bit-plugin",
  "version": "${version}",
  "commit": "${commit}",
  "created_at": "${created_at}",
  "type": "source-integration-release",
  "plugin_name": "aliyun_sls",
  "fluent_bit_integration": "in-tree",
  "dlopen_supported": false,
  "putlogs_compression": "lz4",
  "artifacts": [
    {
      "file": "${archive_file}",
      "sha256": "${archive_sha256}"
    },
    {
      "file": "$(basename "${checksum_file}")"
    }
  ]
}
EOF

echo "created ${archive}"
echo "created ${checksum_file}"
echo "created ${manifest}"
