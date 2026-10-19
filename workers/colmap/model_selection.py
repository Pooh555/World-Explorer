"""Select one reconstruction without dropping another model's frame coverage."""


def select_candidate(image_sets, selected_images):
    if not image_sets or any(
        not images or not images <= selected_images for images in image_sets
    ):
        raise ValueError("Empty model or images outside the selected frame inventory")
    if len(image_sets) == 1:
        return 0
    # A strict superset can replace redundant attempts; incomparable/equal models
    # require recovery rather than guessing which camera solution to trust.
    candidates = [
        index
        for index, images in enumerate(image_sets)
        if all(other < images for j, other in enumerate(image_sets) if j != index)
    ]
    return candidates[0] if len(candidates) == 1 else None
