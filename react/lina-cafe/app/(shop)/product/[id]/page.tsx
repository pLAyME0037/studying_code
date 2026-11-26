import Link from "next/link";
import { notFound } from "next/navigation";
import { getI18n } from "@/app/lib/i18n";
import { getSessionUser } from "@/app/lib/auth";
import { money, firstGlyph, getProduct } from "@/app/lib/shop";
import {
  cartAdd,
  cartBuyNow,
  postReview,
  toggleReviewLike,
} from "@/app/lib/actions";

// GET /product/<id> - four sections: pictures (kind + name) / detail +
// transaction / reviews (thread + likes) + stats / related + recommendation
// (webc_pos serve_shop_product + display/shop/product.h.tt). All lists are
// bounded server-side; the gallery lightbox is the plain
// /js/ProductGallery.js DOM script (no client component).
type PageProps = { params: Promise<{ id: string }> };

export async function generateMetadata({ params }: PageProps) {
  const { id } = await params;
  const viewer = await getSessionUser();
  const p = getProduct(id, viewer ? viewer.id : "");
  // Thrown before the shell flushes: this is what turns a missing product
  // into a real 404 status (the page-level notFound only lands in the
  // streamed part, which commits 200 with the PPR shell already sent).
  if (!p) notFound();
  return { title: p.name };
}

function ReviewMeta({
  user,
  when,
  star,
}: {
  user: string;
  when: string;
  star: string;
}) {
  return (
    <div className="flex items-center gap-3 text-sm text-overlay0 flex-wrap">
      <span className="font-semibold text-base">{user}</span>
      <span>{when}</span>
      {star && <span className="text-blue font-semibold">{star}</span>}
    </div>
  );
}

function LikeButton({
  reviewId,
  liked,
  likes,
}: {
  reviewId: string;
  liked: boolean;
  likes: number;
}) {
  const cls = liked
    ? "text-sm border border-surface0 px-3 py-1 bg-blue text-onbase"
    : "text-sm border border-surface0 px-3 py-1 text-subtext0 hover:border-blue";
  return (
    <form action={toggleReviewLike}>
      <input type="hidden" name="review_id" value={reviewId} />
      <button className={cls}>♥ {likes}</button>
    </form>
  );
}

export default async function ProductPage({ params }: PageProps) {
  const { id } = await params;
  const { tr } = await getI18n();
  const viewer = await getSessionUser();
  const p = getProduct(id, viewer ? viewer.id : "");
  if (!p) notFound();

  const stockI = Math.trunc(p.stock);
  const soldI = p.sold;
  const out = stockI <= 0;
  const avgS = p.ratingCount > 0 ? p.avgRating.toFixed(1) : "";
  const idS = p.id;

  // Labels (tr once, render plain).
  const tRatings = tr("shop.ratings", "ចំណាត់ថ្នាក់");
  const tComments = tr("shop.comments", "មតិ");
  const tSold = tr("shop.sold_count", "បានលក់");
  const tOut = tr("shop.out_of_stock", "អស់ពីរាក់");
  const tIn = tr("shop.in_stock", "នៅសល់: ");
  const tAdd = tr("shop.add_to_cart", "ដាក់ក្នុងរទេះ");
  const tPost = tr("shop.post_comment", "ផ្សាយ");
  // Stock line (template splits class across the if/else arms).
  const stockLine = out ? tOut : tIn + String(stockI);
  const stockLineClass = out ? "text-base  text-red" : "text-base text-green";

  const pics = p.pics;
  const roots = p.roots;
  const replies = p.replies;

  return (
    <div className="flex flex-col gap-3">
      <div className="grid md:grid-cols-3 gap-3 items-start">
        {pics.length > 0 && (
          <section
            id="product-pictures"
            className="bg-mantle border border-surface0 p-3"
            data-gallery
          >
            <div className="bg-blue/10 border border-surface0 flex items-center justify-center">
              <img
                data-gallery-main
                src={pics[0].image}
                alt={pics[0].name}
                className="w-full h-72 md:h-[26rem] object-contain cursor-zoom-in"
              />
            </div>
            <div className="grid grid-cols-4 gap-2 mt-2">
              {pics.map((pic, i) => (
                <button
                  key={i}
                  type="button"
                  data-gallery-thumb
                  data-full={pic.image}
                  className="bg-blue/10 border border-surface0 text-left flex flex-col hover:border-blue"
                  aria-label={pic.name}
                >
                  <img
                    src={pic.image}
                    alt={pic.name}
                    className="w-full h-20 object-cover pointer-events-none"
                  />
                  <span className="px-1.5 py-1 text-xs flex items-center gap-1 min-w-0">
                    <span className="px-1 py-px border border-blue text-blue shrink-0">
                      {pic.kind}
                    </span>
                    <span className="truncate">{pic.name}</span>
                  </span>
                </button>
              ))}
            </div>
            <dialog
              id="gallery-lightbox"
              className="bg-mantle border border-surface0 p-2 w-[92vw] max-w-3xl backdrop:bg-black/70"
            >
              <div className="flex items-center justify-between gap-2 mb-2">
                <button
                  type="button"
                  data-gallery-prev
                  className="border border-surface0 px-3 py-1.5 text-base hover:border-blue"
                  aria-label="previous"
                >
                  ‹
                </button>
                <span
                  data-gallery-caption
                  className="text-sm text-overlay0 truncate flex-1"
                />
                <button
                  type="button"
                  data-gallery-next
                  className="border border-surface0 px-3 py-1.5 text-base hover:border-blue"
                  aria-label="next"
                >
                  ›
                </button>
                <button
                  type="button"
                  data-gallery-close
                  className="border border-surface0 px-3 py-1.5 text-base hover:border-red"
                  aria-label="close"
                >
                  ×
                </button>
              </div>
              <img
                data-gallery-zoom
                src={pics[0].image}
                alt=""
                className="w-full max-h-[75vh] object-contain bg-blue/10"
              />
            </dialog>
          </section>
        )}

        <section
          id="product-detail"
          className="bg-mantle border border-surface0 px-4 py-4 flex flex-col gap-3"
        >
          {pics.length === 0 && (
            <div className="bg-blue/10 h-40 flex items-center justify-center text-blue font-bold text-5xl">
              {firstGlyph(p.name)}
            </div>
          )}
          <div className="text-sm text-overlay0">
            SKU: {p.sku} · {p.cat}
          </div>
          <h1 className="text-2xl font-semibold leading-snug">{p.name}</h1>
          {p.ratingCount > 0 && (
            <div className="text-sm text-overlay0 flex items-center gap-2 flex-wrap">
              <span className="text-blue font-semibold">★ {avgS}</span>
              <span>·</span>
              <span>
                {p.ratingCount} {tRatings}
              </span>
              <span>·</span>
              <span>
                {p.commentCount} {tComments}
              </span>
            </div>
          )}
          <div className="flex items-baseline gap-3 flex-wrap">
            <div className="text-3xl font-bold text-blue">{money(p.price)}</div>
            <span className="text-sm text-overlay0">
              {soldI} {tSold}
            </span>
          </div>
          <div className={stockLineClass}>{stockLine}</div>
          {out ? (
            <div className="bg-red/10 border-l-2 border-red text-red px-3 py-2 text-sm">
              {tr("shop.out_notice", "ផលិតផលនេះអស់ពីរាក់")}
            </div>
          ) : (
            <div className="flex items-center gap-2 flex-wrap">
              <form action={cartAdd} className="flex items-center gap-2">
                <input type="hidden" name="product_id" value={idS} />
                <input
                  type="hidden"
                  name="redirect"
                  value={`/product/${idS}`}
                />
                <input
                  type="number"
                  name="qty"
                  defaultValue="1"
                  min="1"
                  max={stockI}
                  className="border border-surface0 bg-mantle px-2.5 py-2 text-base w-24"
                />
                <button className="bg-blue text-onbase hover:brightness-90 px-4 py-2.5 font-semibold">
                  {tAdd}
                </button>
              </form>
              <form action={cartBuyNow} className="flex items-center gap-2">
                <input type="hidden" name="product_id" value={idS} />
                <button className="bg-mauve text-onbase hover:brightness-90 px-5 py-2.5 font-semibold">
                  {tr("shop.buy_now", "ទិញឥឡូវនេះ")}
                </button>
              </form>
            </div>
          )}
          <Link href="/" className="text-sm text-blue hover:underline mt-1">
            {tr("shop.back_list", "« ត្រឡប់ទៅបញ្ជីផលិតផល")}
          </Link>
        </section>

        <section
          id="user-stats"
          className="bg-mantle border border-surface0 px-4 py-4 flex flex-col gap-3"
        >
          <h2 className="text-base font-semibold">
            {tr("shop.user_stats", "មតិអ្នកប្រើប្រាស់")}
          </h2>
          {p.ratingCount > 0 && (
            <>
              <div className="flex items-baseline gap-2">
                <span className="text-4xl font-bold text-blue">★ {avgS}</span>
                <span className="text-sm text-overlay0">
                  {p.ratingCount} {tRatings}
                </span>
              </div>
              <div className="flex flex-col gap-1.5">
                {[5, 4, 3, 2, 1].map((s) => {
                  const count = p.starCount[s];
                  const spct = Math.trunc((count * 100) / p.ratingCount);
                  return (
                    <div
                      key={s}
                      className="flex items-center gap-2 text-sm"
                      data-star-bar={s}
                    >
                      <span className="w-10 text-right shrink-0">{s} ★</span>
                      <div className="flex-1 h-2.5 bg-surface0 rounded overflow-hidden">
                        <div
                          className="h-2.5 bg-blue rounded"
                          style={{ width: `${spct}%` }}
                        />
                      </div>
                      <span className="w-8 text-right text-overlay0 shrink-0">
                        {count}
                      </span>
                    </div>
                  );
                })}
              </div>
            </>
          )}
          <div className="text-sm text-overlay0">
            {p.commentCount} {tComments} · {soldI} {tSold}
          </div>
        </section>
      </div>

      <section
        id="product-reviews"
        className="bg-mantle border border-surface0 px-4 py-4 flex flex-col gap-4"
      >
        <h2 className="text-base font-semibold">
          {tr("shop.reviews", "មតិយោបល់")}{" "}
          <span className="text-sm font-normal text-overlay0">
            ({p.commentCount})
          </span>
        </h2>

        {viewer !== null ? (
          <form
            action={postReview}
            className="border border-surface0 bg-blue/5 px-3 py-3 flex flex-col gap-2"
          >
            <input type="hidden" name="product_id" value={idS} />
            <textarea
              name="body"
              rows={3}
              maxLength={1000}
              required
              placeholder={tr("shop.write_comment", "សរសេរមតិរបស់អ្នក...")}
              className="border border-surface0 bg-mantle text-base px-2.5 py-2"
            />
            <div className="flex items-center gap-2">
              <select
                name="rating"
                className="border border-surface0 bg-mantle text-base px-2 py-2"
              >
                <option value="">
                  {tr("shop.no_rating", "គ្មានចំណាត់ថ្នាក់")}
                </option>
                {[1, 2, 3, 4, 5].map((rr) => (
                  <option key={rr} value={rr}>
                    {rr} ★
                  </option>
                ))}
              </select>
              <button className="bg-blue hover:brightness-90 px-4 py-2 text-base">
                {tPost}
              </button>
            </div>
          </form>
        ) : (
          <a
            href={`/login?next=/product/${idS}`}
            className="self-start border border-surface0 bg-blue/5 px-3 py-2 text-sm text-blue hover:underline"
          >
            {tr(
              "shop.login_to_comment",
              "ចូលគណនីដើម្បីផ្សាយមតិ និង LIKE",
            )}
          </a>
        )}

        {p.commentCount === 0 && (
          <div className="text-sm text-overlay0">
            {tr("shop.no_comments", "មិនទាន់មានមតិនៅឡើយ")}
          </div>
        )}

        {roots.map((rv) => {
          const star = rv.rating > 0 ? `★ ${rv.rating.toFixed(0)}` : "";
          return (
            <article
              key={rv.id}
              className="border-t border-surface0 pt-3"
              data-review-id={rv.id}
            >
              <ReviewMeta user={rv.user} when={rv.when} star={star} />
              <div className="text-base whitespace-pre-line my-2">{rv.body}</div>
              <div className="flex items-center gap-4">
                <LikeButton reviewId={rv.id} liked={rv.liked} likes={rv.likes} />
                {viewer !== null && (
                  <details className="text-sm">
                    <summary className="text-blue cursor-pointer">
                      {tr("shop.reply", "ឆ្លើយតប")}
                    </summary>
                    <form
                      action={postReview}
                      className="flex flex-col gap-1 mt-1 max-w-2xl"
                    >
                      <input type="hidden" name="product_id" value={idS} />
                      <input type="hidden" name="parent_id" value={rv.id} />
                      <textarea
                        name="body"
                        rows={2}
                        maxLength={1000}
                        required
                        placeholder={tr("shop.write_reply", "ឆ្លើយតប...")}
                        className="border border-surface0 bg-mantle text-base px-2.5 py-2"
                      />
                      <button className="self-start bg-blue text-onbase hover:brightness-90 px-3 py-1.5 text-sm">
                        {tPost}
                      </button>
                    </form>
                  </details>
                )}
              </div>

              {replies
                .filter((rp) => rp.parent === rv.id)
                .map((rp) => (
                  <article
                    key={rp.id}
                    className="border-l-2 border-surface0 pl-3 mt-3"
                    data-review-id={rp.id}
                  >
                    <ReviewMeta user={rp.user} when={rp.when} star="" />
                    <div className="text-base whitespace-pre-line my-1">
                      {rp.body}
                    </div>
                    <LikeButton
                      reviewId={rp.id}
                      liked={rp.liked}
                      likes={rp.likes}
                    />
                  </article>
                ))}
            </article>
          );
        })}
      </section>

      <section id="product-related" className="flex flex-col gap-3">
        <div className="bg-mantle border border-surface0 px-4 py-4">
          <h2 className="text-base font-semibold mb-3">
            {tr("shop.related", "ផលិតផលទាក់ទង")}
          </h2>
          {p.related.length === 0 ? (
            <div className="text-sm text-overlay0">
              {tr("shop.no_related", "មិនមានផលិតផលទាក់ទង")}
            </div>
          ) : (
            <div className="grid grid-cols-2 md:grid-cols-3 gap-3">
              {p.related.map((c) => (
                <a
                  key={c.id}
                  href={`/product/${c.id}`}
                  className="bg-blue/10 border border-surface0 px-3 py-3 flex flex-col gap-2 hover:border-blue"
                >
                  {c.image ? (
                    <img
                      src={c.image}
                      alt={c.name}
                      className="w-full h-44 object-cover"
                    />
                  ) : (
                    <div className="w-full h-44 flex items-center justify-center text-blue font-bold text-4xl">
                      {firstGlyph(c.name)}
                    </div>
                  )}
                  <span className="text-base leading-snug line-clamp-2">
                    {c.name}
                  </span>
                  <span className="text-lg font-semibold text-blue">
                    {money(c.price)}
                  </span>
                </a>
              ))}
            </div>
          )}
        </div>

        <div className="bg-mantle border border-surface0 px-4 py-4">
          <h2 className="text-base font-semibold mb-3">
            {tr("shop.recommended", "ផលិតផលដែលគេទិញច្រើន")}
          </h2>
          {p.recommend.length === 0 ? (
            <div className="text-sm text-overlay0">
              {tr("shop.no_recommended", "មិនទាន់មានទិន្នន័យ")}
            </div>
          ) : (
            <div className="grid grid-cols-2 md:grid-cols-3 gap-3">
              {p.recommend.map((c) => (
                <a
                  key={c.id}
                  href={`/product/${c.id}`}
                  className="bg-blue/10 border border-surface0 px-3 py-3 flex flex-col gap-2 hover:border-blue"
                >
                  {c.image ? (
                    <img
                      src={c.image}
                      alt={c.name}
                      className="w-full h-44 object-cover"
                    />
                  ) : (
                    <div className="w-full h-44 flex items-center justify-center text-blue font-bold text-4xl">
                      {firstGlyph(c.name)}
                    </div>
                  )}
                  <span className="text-base leading-snug line-clamp-2">
                    {c.name}
                  </span>
                  <span className="text-lg font-semibold text-blue">
                    {money(c.price)}
                  </span>
                  <span className="text-sm text-overlay0">
                    {c.sold} {tSold}
                  </span>
                </a>
              ))}
            </div>
          )}
        </div>
      </section>

      {pics.length > 0 && (
        <script src="/js/ProductGallery.js" defer />
      )}
    </div>
  );
}
